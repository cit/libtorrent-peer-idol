/*

Copyright (c) 2003-2012, Arvid Norberg
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions
are met:

    * Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above copyright
      notice, this list of conditions and the following disclaimer in
      the documentation and/or other materials provided with the distribution.
    * Neither the name of the author nor the names of its
      contributors may be used to endorse or promote products derived
      from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
POSSIBILITY OF SUCH DAMAGE.

*/

#include "libtorrent/pch.hpp"

#include <ctime>
#include <iterator>
#include <algorithm>
#include <set>
#include <cctype>
#include <algorithm>

#ifdef _MSC_VER
#pragma warning(push, 1)
#endif

#include <boost/optional.hpp>
#include <boost/bind.hpp>

#ifdef _MSC_VER
#pragma warning(pop)
#endif

#include "libtorrent/peer_id.hpp"
#include "libtorrent/bt_peer_connection.hpp"
#include "libtorrent/torrent_info.hpp"
#include "libtorrent/tracker_manager.hpp"
#include "libtorrent/bencode.hpp"
#include "libtorrent/hasher.hpp"
#include "libtorrent/entry.hpp"
#include "libtorrent/session.hpp"
#include "libtorrent/aux_/session_impl.hpp"
#include "libtorrent/invariant_check.hpp"
#include "libtorrent/utf8.hpp"
#include "libtorrent/thread.hpp"

#if defined(_MSC_VER) && _MSC_VER < 1300
namespace std
{
	using ::srand;
	using ::isalnum;
};
#endif

using libtorrent::aux::session_impl;

namespace libtorrent
{

	torrent_status::torrent_status()
		: state(checking_resume_data)
		, paused(false)
		, auto_managed(false)
		, sequential_download(false)
		, is_seeding(false)
		, is_finished(false)
		, has_metadata(false)
		, progress(0.f)
		, progress_ppm(0)
		, total_download(0)
		, total_upload(0)
		, total_payload_download(0)
		, total_payload_upload(0)
		, total_failed_bytes(0)
		, total_redundant_bytes(0)
		, download_rate(0)
		, upload_rate(0)
		, download_payload_rate(0)
		, upload_payload_rate(0)
		, num_seeds(0)
		, num_peers(0)
		, num_complete(-1)
		, num_incomplete(-1)
		, list_seeds(0)
		, list_peers(0)
		, connect_candidates(0)
		, num_pieces(0)
		, total_done(0)
		, total_wanted_done(0)
		, total_wanted(0)
		, distributed_full_copies(0)
		, distributed_fraction(0)
		, distributed_copies(0.f)
		, block_size(0)
		, num_uploads(0)
		, num_connections(0)
		, uploads_limit(0)
		, connections_limit(0)
		, storage_mode(storage_mode_sparse)
		, up_bandwidth_queue(0)
		, down_bandwidth_queue(0)
		, all_time_upload(0)
		, all_time_download(0)
		, active_time(0)
		, finished_time(0)
		, seeding_time(0)
		, seed_rank(0)
		, last_scrape(0)
		, has_incoming(false)
		, sparse_regions(0)
		, seed_mode(false)
		, upload_mode(false)
		, share_mode(false)
		, super_seeding(false)
		, priority(0)
		, added_time(0)
		, completed_time(0)
		, last_seen_complete(0)
		, time_since_upload(0)
		, time_since_download(0)
		, queue_position(0)
		, need_save_resume(false)
		, ip_filter_applies(true)
		, info_hash(0)
		, listen_port(0)
	{}

	torrent_status::~torrent_status() {}

	template <class R>
	void fun_ret(R* ret, bool* done, condition* e, mutex* m, boost::function<R(void)> f)
	{
		*ret = f();
		mutex::scoped_lock l(*m);
		*done = true;
		e->signal_all(l);
	}

	// defined in session.cpp
	void fun_wrap(bool* done, condition* e, mutex* m, boost::function<void(void)> f);

#define TORRENT_ASYNC_CALL(x) \
	boost::shared_ptr<torrent> t = m_torrent.lock(); \
	if (!t) return; \
	session_impl& ses = t->session(); \
	ses.m_io_service.post(boost::bind(&torrent:: x, t))

#define TORRENT_ASYNC_CALL1(x, a1) \
	boost::shared_ptr<torrent> t = m_torrent.lock(); \
	if (!t) return; \
	session_impl& ses = t->session(); \
	ses.m_io_service.post(boost::bind(&torrent:: x, t, a1))

#define TORRENT_ASYNC_CALL2(x, a1, a2) \
	boost::shared_ptr<torrent> t = m_torrent.lock(); \
	if (!t) return; \
	session_impl& ses = t->session(); \
	ses.m_io_service.post(boost::bind(&torrent:: x, t, a1, a2))

#define TORRENT_ASYNC_CALL3(x, a1, a2, a3) \
	boost::shared_ptr<torrent> t = m_torrent.lock(); \
	if (!t) return; \
	session_impl& ses = t->session(); \
	ses.m_io_service.post(boost::bind(&torrent:: x, t, a1, a2, a3))

#define TORRENT_ASYNC_CALL4(x, a1, a2, a3, a4) \
	boost::shared_ptr<torrent> t = m_torrent.lock(); \
	if (!t) return; \
	session_impl& ses = t->session(); \
	ses.m_io_service.post(boost::bind(&torrent:: x, t, a1, a2, a3, a4))

#define TORRENT_SYNC_CALL(x) \
	boost::shared_ptr<torrent> t = m_torrent.lock(); \
	if (!t) return; \
	bool done = false; \
	session_impl& ses = t->session(); \
	mutex::scoped_lock l(ses.mut); \
	ses.m_io_service.post(boost::bind(&fun_wrap, &done, &ses.cond, &ses.mut, boost::function<void(void)>(boost::bind(&torrent:: x, t)))); \
	do { ses.cond.wait(l); } while(!done)

#define TORRENT_SYNC_CALL1(x, a1) \
	boost::shared_ptr<torrent> t = m_torrent.lock(); \
	if (t) { \
	bool done = false; \
	session_impl& ses = t->session(); \
	mutex::scoped_lock l(ses.mut); \
	ses.m_io_service.post(boost::bind(&fun_wrap, &done, &ses.cond, &ses.mut, boost::function<void(void)>(boost::bind(&torrent:: x, t, a1)))); \
	t.reset(); \
	do { ses.cond.wait(l); } while(!done); }

#define TORRENT_SYNC_CALL2(x, a1, a2) \
	boost::shared_ptr<torrent> t = m_torrent.lock(); \
	if (t) { \
	bool done = false; \
	session_impl& ses = t->session(); \
	mutex::scoped_lock l(ses.mut); \
	ses.m_io_service.post(boost::bind(&fun_wrap, &done, &ses.cond, &ses.mut, boost::function<void(void)>(boost::bind(&torrent:: x, t, a1, a2)))); \
	t.reset(); \
	do { ses.cond.wait(l); } while(!done); }

#define TORRENT_SYNC_CALL3(x, a1, a2, a3) \
	boost::shared_ptr<torrent> t = m_torrent.lock(); \
	if (t) { \
	bool done = false; \
	session_impl& ses = t->session(); \
	mutex::scoped_lock l(ses.mut); \
	ses.m_io_service.post(boost::bind(&fun_wrap, &done, &ses.cond, &ses.mut, boost::function<void(void)>(boost::bind(&torrent:: x, t, a1, a2, a3)))); \
	t.reset(); \
	do { ses.cond.wait(l); } while(!done); }

#define TORRENT_SYNC_CALL_RET(type, def, x) \
	boost::shared_ptr<torrent> t = m_torrent.lock(); \
	if (!t) return def; \
	bool done = false; \
	session_impl& ses = t->session(); \
	type r; \
	mutex::scoped_lock l(ses.mut); \
	ses.m_io_service.post(boost::bind(&fun_ret<type >, &r, &done, &ses.cond, &ses.mut, boost::function<type(void)>(boost::bind(&torrent:: x, t)))); \
	t.reset(); \
	do { ses.cond.wait(l); } while(!done)

#define TORRENT_SYNC_CALL_RET1(type, def, x, a1) \
	boost::shared_ptr<torrent> t = m_torrent.lock(); \
	if (!t) return def; \
	bool done = false; \
	session_impl& ses = t->session(); \
	type r; \
	mutex::scoped_lock l(ses.mut); \
	ses.m_io_service.post(boost::bind(&fun_ret<type >, &r, &done, &ses.cond, &ses.mut, boost::function<type(void)>(boost::bind(&torrent:: x, t, a1)))); \
	t.reset(); \
	do { ses.cond.wait(l); } while(!done)

#define TORRENT_SYNC_CALL_RET2(type, def, x, a1, a2) \
	boost::shared_ptr<torrent> t = m_torrent.lock(); \
	if (!t) return def; \
	bool done = false; \
	session_impl& ses = t->session(); \
	type r; \
	mutex::scoped_lock l(ses.mut); \
	ses.m_io_service.post(boost::bind(&fun_ret<type >, &r, &done, &ses.cond, &ses.mut, boost::function<type(void)>(boost::bind(&torrent:: x, t, a1, a2)))); \
	t.reset(); \
	do { ses.cond.wait(l); } while(!done)

#ifndef BOOST_NO_EXCEPTIONS
	void throw_invalid_handle()
	{
		throw libtorrent_exception(errors::invalid_torrent_handle);
	}
#endif

#ifdef TORRENT_DEBUG

	void torrent_handle::check_invariant() const
	{}

#endif

	sha1_hash torrent_handle::info_hash() const
	{
		INVARIANT_CHECK;
		const static sha1_hash empty;
		TORRENT_SYNC_CALL_RET(sha1_hash, empty, info_hash);
		return r;
	}

	int torrent_handle::max_uploads() const
	{
		INVARIANT_CHECK;
		TORRENT_SYNC_CALL_RET(int, 0, max_uploads);
		return r;
	}

	void torrent_handle::set_max_uploads(int max_uploads) const
	{
		INVARIANT_CHECK;
		TORRENT_ASSERT(max_uploads >= 2 || max_uploads == -1);
		TORRENT_ASYNC_CALL2(set_max_uploads, max_uploads, true);
	}

	int torrent_handle::max_connections() const
	{
		INVARIANT_CHECK;
		TORRENT_SYNC_CALL_RET(int, 0, max_connections);
		return r;
	}

	void torrent_handle::set_max_connections(int max_connections) const
	{
		INVARIANT_CHECK;
		TORRENT_ASSERT(max_connections >= 2 || max_connections == -1);
		TORRENT_ASYNC_CALL2(set_max_connections, max_connections, true);
	}

	void torrent_handle::set_upload_limit(int limit) const
	{
		INVARIANT_CHECK;
		TORRENT_ASSERT(limit >= -1);
		TORRENT_ASYNC_CALL2(set_upload_limit, limit, true);
	}

	int torrent_handle::upload_limit() const
	{
		INVARIANT_CHECK;
		TORRENT_SYNC_CALL_RET(int, 0, upload_limit);
		return r;
	}

	void torrent_handle::set_download_limit(int limit) const
	{
		INVARIANT_CHECK;
		TORRENT_ASSERT(limit >= -1);
		TORRENT_ASYNC_CALL2(set_download_limit, limit, true);
	}

	int torrent_handle::download_limit() const
	{
		INVARIANT_CHECK;
		TORRENT_SYNC_CALL_RET(int, 0, download_limit);
		return r;
	}

	void torrent_handle::move_storage(
		std::string const& save_path) const
	{
		INVARIANT_CHECK;
		TORRENT_ASYNC_CALL1(move_storage, save_path);
	}

#if TORRENT_USE_WSTRING
	void torrent_handle::move_storage(
		std::wstring const& save_path) const
	{
		INVARIANT_CHECK;
		std::string utf8;
		wchar_utf8(save_path, utf8);
		TORRENT_ASYNC_CALL1(move_storage, utf8);
	}

	void torrent_handle::rename_file(int index, std::wstring const& new_name) const
	{
		INVARIANT_CHECK;
		std::string utf8;
		wchar_utf8(new_name, utf8);
		TORRENT_ASYNC_CALL2(rename_file, index, utf8);
	}
#endif // TORRENT_USE_WSTRING

	void torrent_handle::rename_file(int index, std::string const& new_name) const
	{
		INVARIANT_CHECK;
		TORRENT_ASYNC_CALL2(rename_file, index, new_name);
	}

#ifndef TORRENT_DISABLE_EXTENSIONS
	void torrent_handle::add_extension(
		boost::function<boost::shared_ptr<torrent_plugin>(torrent*, void*)> const& ext
		, void* userdata)
	{
		INVARIANT_CHECK;
		TORRENT_ASYNC_CALL2(add_extension, ext, userdata);
	}
#endif

	bool torrent_handle::set_metadata(char const* metadata, int size) const
	{
		INVARIANT_CHECK;
		TORRENT_SYNC_CALL_RET2(bool, false, set_metadata, metadata, size);
		return r;
	}

	void torrent_handle::pause(int flags) const
	{
		INVARIANT_CHECK;
		TORRENT_ASYNC_CALL1(pause, bool(flags & graceful_pause));
	}

	void torrent_handle::apply_ip_filter(bool b) const
	{
		INVARIANT_CHECK;
		TORRENT_ASYNC_CALL1(set_apply_ip_filter, b);
	}

	void torrent_handle::set_share_mode(bool b) const
	{
		INVARIANT_CHECK;
		TORRENT_ASYNC_CALL1(set_share_mode, b);
	}

	void torrent_handle::set_upload_mode(bool b) const
	{
		INVARIANT_CHECK;
		TORRENT_ASYNC_CALL1(set_upload_mode, b);
	}

	void torrent_handle::flush_cache() const
	{
		INVARIANT_CHECK;
		TORRENT_ASYNC_CALL(flush_cache);
	}

	void torrent_handle::set_ssl_certificate(
		std::string const& certificate
		, std::string const& private_key
		, std::string const& dh_params
		, std::string const& passphrase)
	{
#ifdef TORRENT_USE_OPENSSL
		TORRENT_ASYNC_CALL4(set_ssl_cert, certificate, private_key, dh_params, passphrase);
#endif
	}

	void torrent_handle::save_resume_data(int f) const
	{
		INVARIANT_CHECK;
		TORRENT_ASYNC_CALL1(save_resume_data, f);
	}

	bool torrent_handle::need_save_resume_data() const
	{
		INVARIANT_CHECK;
		TORRENT_SYNC_CALL_RET(bool, false, need_save_resume_data);
		return r;
	}

	void torrent_handle::force_recheck() const
	{
		INVARIANT_CHECK;
		TORRENT_ASYNC_CALL(force_recheck);
	}

	void torrent_handle::resume() const
	{
		INVARIANT_CHECK;
		TORRENT_ASYNC_CALL(resume);
	}

	void torrent_handle::auto_managed(bool m) const
	{
		INVARIANT_CHECK;
		TORRENT_ASYNC_CALL1(auto_managed, m);
	}

	void torrent_handle::set_priority(int p) const
	{
		INVARIANT_CHECK;
		TORRENT_ASYNC_CALL1(set_priority, p);
	}

	int torrent_handle::queue_position() const
	{
		INVARIANT_CHECK;
		TORRENT_SYNC_CALL_RET(int, -1, queue_position);
		return r;
	}

	void torrent_handle::queue_position_up() const
	{
		INVARIANT_CHECK;
		TORRENT_ASYNC_CALL(queue_up);
	}

	void torrent_handle::queue_position_down() const
	{
		INVARIANT_CHECK;
		TORRENT_ASYNC_CALL(queue_down);
	}

	void torrent_handle::queue_position_top() const
	{
		INVARIANT_CHECK;
		TORRENT_ASYNC_CALL1(set_queue_position, 0);
	}

	void torrent_handle::queue_position_bottom() const
	{
		INVARIANT_CHECK;
		TORRENT_ASYNC_CALL1(set_queue_position, INT_MAX);
	}

	void torrent_handle::clear_error() const
	{
		INVARIANT_CHECK;
		TORRENT_ASYNC_CALL(clear_error);
	}

	void torrent_handle::set_tracker_login(std::string const& name
		, std::string const& password) const
	{
		INVARIANT_CHECK;
		TORRENT_ASYNC_CALL2(set_tracker_login, name, password);
	}

	void torrent_handle::file_progress(std::vector<size_type>& progress, int flags) const
	{
		INVARIANT_CHECK;
		TORRENT_SYNC_CALL2(file_progress, boost::ref(progress), flags);
	}

	torrent_status torrent_handle::status(boost::uint32_t flags) const
	{
		INVARIANT_CHECK;
		torrent_status st;
		TORRENT_SYNC_CALL2(status, &st, flags);
		return st;
	}

	void torrent_handle::set_sequential_download(bool sd) const
	{
		INVARIANT_CHECK;
		TORRENT_ASYNC_CALL1(set_sequential_download, sd);
	}

	std::string torrent_handle::name() const
	{
		INVARIANT_CHECK;
		TORRENT_SYNC_CALL_RET(std::string, "", name);
		return r;
	}

	void torrent_handle::piece_availability(std::vector<int>& avail) const
	{
		INVARIANT_CHECK;
		TORRENT_SYNC_CALL1(piece_availability, boost::ref(avail));
	}

	void torrent_handle::piece_priority(int index, int priority) const
	{
		INVARIANT_CHECK;
		TORRENT_ASYNC_CALL2(set_piece_priority, index, priority);
	}

	int torrent_handle::piece_priority(int index) const
	{
		INVARIANT_CHECK;
		TORRENT_SYNC_CALL_RET1(int, 0, piece_priority, index);
		return r;
	}

	void torrent_handle::prioritize_pieces(std::vector<int> const& pieces) const
	{
		INVARIANT_CHECK;
		TORRENT_ASYNC_CALL1(prioritize_pieces, pieces);
	}

	std::vector<int> torrent_handle::piece_priorities() const
	{
		INVARIANT_CHECK;
		std::vector<int> ret;
		TORRENT_SYNC_CALL1(piece_priorities, &ret);
		return ret;
	}

	void torrent_handle::file_priority(int index, int priority) const
	{
		INVARIANT_CHECK;
		TORRENT_ASYNC_CALL2(set_file_priority, index, priority);
	}

	int torrent_handle::file_priority(int index) const
	{
		INVARIANT_CHECK;
		TORRENT_SYNC_CALL_RET1(int, 0, file_priority, index);
		return r;
	}

	void torrent_handle::prioritize_files(std::vector<int> const& files) const
	{
		INVARIANT_CHECK;
		TORRENT_ASYNC_CALL1(prioritize_files, files);
	}

	std::vector<int> torrent_handle::file_priorities() const
	{
		INVARIANT_CHECK;
		std::vector<int> ret;
		TORRENT_SYNC_CALL1(file_priorities, &ret);
		return ret;
	}

#ifndef TORRENT_NO_DEPRECATE
// ============ start deprecation ===============

	void torrent_handle::use_interface(const char* net_interface) const
	{
		INVARIANT_CHECK;
		TORRENT_ASYNC_CALL1(use_interface, std::string(net_interface));
	}

#if !TORRENT_NO_FPU
	void torrent_handle::file_progress(std::vector<float>& progress) const
	{
		INVARIANT_CHECK;
		TORRENT_SYNC_CALL1(file_progress, boost::ref(progress));
	}
#endif

	int torrent_handle::get_peer_upload_limit(tcp::endpoint ip) const
	{
		INVARIANT_CHECK;
		TORRENT_SYNC_CALL_RET1(int, -1, get_peer_upload_limit, ip);
		return r;
	}

	int torrent_handle::get_peer_download_limit(tcp::endpoint ip) const
	{
		INVARIANT_CHECK;
		TORRENT_SYNC_CALL_RET1(int, -1, get_peer_download_limit, ip);
		return r;
	}

	void torrent_handle::set_peer_upload_limit(tcp::endpoint ip, int limit) const
	{
		INVARIANT_CHECK;
		TORRENT_ASSERT(limit >= -1);
		TORRENT_ASYNC_CALL2(set_peer_upload_limit, ip, limit);
	}

	void torrent_handle::set_peer_download_limit(tcp::endpoint ip, int limit) const
	{
		INVARIANT_CHECK;
		TORRENT_ASSERT(limit >= -1);
		TORRENT_ASYNC_CALL2(set_peer_download_limit, ip, limit);
	}

	void torrent_handle::set_ratio(float ratio) const
	{
		INVARIANT_CHECK;
		
		TORRENT_ASSERT(ratio >= 0.f);
		if (ratio < 1.f && ratio > 0.f)
			ratio = 1.f;
		TORRENT_ASYNC_CALL1(set_ratio, ratio);
	}

	bool torrent_handle::is_seed() const
	{
		INVARIANT_CHECK;
		TORRENT_SYNC_CALL_RET(bool, false, is_seed);
		return r;
	}

	bool torrent_handle::is_finished() const
	{
		INVARIANT_CHECK;
		TORRENT_SYNC_CALL_RET(bool, false, is_finished);
		return r;
	}

	bool torrent_handle::is_paused() const
	{
		INVARIANT_CHECK;
		TORRENT_SYNC_CALL_RET(bool, false, is_torrent_paused);
		return r;
	}

	bool torrent_handle::is_sequential_download() const
	{
		INVARIANT_CHECK;
		TORRENT_SYNC_CALL_RET(bool, false, is_sequential_download);
		return r;
	}

	bool torrent_handle::is_auto_managed() const
	{
		INVARIANT_CHECK;
		TORRENT_SYNC_CALL_RET(bool, false, is_auto_managed);
		return r;
	}

	bool torrent_handle::has_metadata() const
	{
		INVARIANT_CHECK;
		TORRENT_SYNC_CALL_RET(bool, false, valid_metadata);
		return r;
	}

	void torrent_handle::filter_piece(int index, bool filter) const
	{
		INVARIANT_CHECK;
		TORRENT_ASYNC_CALL2(filter_piece, index, filter);
	}

	void torrent_handle::filter_pieces(std::vector<bool> const& pieces) const
	{
		INVARIANT_CHECK;
		TORRENT_ASYNC_CALL1(filter_pieces, pieces);
	}

	bool torrent_handle::is_piece_filtered(int index) const
	{
		INVARIANT_CHECK;
		TORRENT_SYNC_CALL_RET1(bool, false, is_piece_filtered, index);
		return r;
	}

	std::vector<bool> torrent_handle::filtered_pieces() const
	{
		INVARIANT_CHECK;
		std::vector<bool> ret;
		TORRENT_SYNC_CALL1(filtered_pieces, ret);
		return ret;
	}

	void torrent_handle::filter_files(std::vector<bool> const& files) const
	{
		INVARIANT_CHECK;
		TORRENT_ASYNC_CALL1(filter_files, files);
	}

	bool torrent_handle::super_seeding() const
	{
		INVARIANT_CHECK;
		TORRENT_SYNC_CALL_RET(bool, false, super_seeding);
		return r;
	}

// ============ end deprecation ===============
#endif

	std::vector<announce_entry> torrent_handle::trackers() const
	{
		INVARIANT_CHECK;
		const static std::vector<announce_entry> empty;
		TORRENT_SYNC_CALL_RET(std::vector<announce_entry>, empty, trackers);
		return r;
	}

	void torrent_handle::add_url_seed(std::string const& url) const
	{
		INVARIANT_CHECK;
		TORRENT_ASYNC_CALL2(add_web_seed, url, web_seed_entry::url_seed);
	}

	void torrent_handle::remove_url_seed(std::string const& url) const
	{
		INVARIANT_CHECK;
		TORRENT_ASYNC_CALL2(remove_web_seed, url, web_seed_entry::url_seed);
	}

	std::set<std::string> torrent_handle::url_seeds() const
	{
		INVARIANT_CHECK;
		const static std::set<std::string> empty;
		TORRENT_SYNC_CALL_RET1(std::set<std::string>, empty, web_seeds, web_seed_entry::url_seed);
		return r;
	}

	void torrent_handle::add_http_seed(std::string const& url) const
	{
		INVARIANT_CHECK;
		TORRENT_ASYNC_CALL2(add_web_seed, url, web_seed_entry::http_seed);
	}

	void torrent_handle::remove_http_seed(std::string const& url) const
	{
		INVARIANT_CHECK;
		TORRENT_ASYNC_CALL2(remove_web_seed, url, web_seed_entry::http_seed);
	}

	std::set<std::string> torrent_handle::http_seeds() const
	{
		INVARIANT_CHECK;
		const static std::set<std::string> empty;
		TORRENT_SYNC_CALL_RET1(std::set<std::string>, empty, web_seeds, web_seed_entry::http_seed);
		return r;
	}

	void torrent_handle::replace_trackers(
		std::vector<announce_entry> const& urls) const
	{
		INVARIANT_CHECK;
		TORRENT_ASYNC_CALL1(replace_trackers, urls);
	}

	void torrent_handle::add_tracker(announce_entry const& url) const
	{
		INVARIANT_CHECK;
		TORRENT_ASYNC_CALL1(add_tracker, url);
	}

	void torrent_handle::add_piece(int piece, char const* data, int flags) const
	{
		INVARIANT_CHECK;
		TORRENT_SYNC_CALL3(add_piece, piece, data, flags);
	}

	void torrent_handle::read_piece(int piece) const
	{
		INVARIANT_CHECK;
		TORRENT_ASYNC_CALL1(read_piece, piece);
	}

	bool torrent_handle::have_piece(int piece) const
	{
		INVARIANT_CHECK;
		TORRENT_SYNC_CALL_RET1(bool, false, have_piece, piece);
		return r;
	}

	storage_interface* torrent_handle::get_storage_impl() const
	{
		INVARIANT_CHECK;
		TORRENT_SYNC_CALL_RET(storage_interface*, 0, get_storage);
		return r;
	}

	bool torrent_handle::is_valid() const
	{
		INVARIANT_CHECK;
		return !m_torrent.expired();
	}

	boost::intrusive_ptr<torrent_info> torrent_handle::torrent_file() const
	{
		INVARIANT_CHECK;
		TORRENT_SYNC_CALL_RET(boost::intrusive_ptr<torrent_info>, boost::intrusive_ptr<torrent_info>(), get_torrent_copy);
		return r;
	}

#ifndef TORRENT_NO_DEPRECATE
	// this function should either be removed, or return
	// reference counted handle to the torrent_info which
	// forces the torrent to stay loaded while the client holds it
	torrent_info const& torrent_handle::get_torrent_info() const
	{
		INVARIANT_CHECK;
#ifdef BOOST_NO_EXCEPTIONS
		const static torrent_info empty(sha1_hash(0));
#endif
		boost::shared_ptr<torrent> t = m_torrent.lock();
		if (!t)
#ifdef BOOST_NO_EXCEPTIONS
			return empty;
#else
			throw_invalid_handle();
#endif
		if (!t->valid_metadata())
#ifdef BOOST_NO_EXCEPTIONS
			return empty;
#else
			throw_invalid_handle();
#endif
		return t->torrent_file();
	}

	entry torrent_handle::write_resume_data() const
	{
		INVARIANT_CHECK;

		entry ret(entry::dictionary_t);
		TORRENT_SYNC_CALL1(write_resume_data, boost::ref(ret));
		t = m_torrent.lock();
		if (t)
		{
			bool done = false;
			session_impl& ses = t->session();
			mutex::scoped_lock l(ses.mut);
			ses.m_io_service.post(boost::bind(&fun_wrap, &done, &ses.cond
				, &ses.mut, boost::function<void(void)>(boost::bind(
					&piece_manager::write_resume_data, &t->filesystem(), boost::ref(ret)))));
			t.reset();
			do { ses.cond.wait(l); } while(!done);
		}

		return ret;
	}
#endif

	std::string torrent_handle::save_path() const
	{
		INVARIANT_CHECK;
		TORRENT_SYNC_CALL_RET(std::string, "", save_path);
		return r;
	}

	void torrent_handle::connect_peer(tcp::endpoint const& adr, int source) const
	{
		INVARIANT_CHECK;
		TORRENT_ASYNC_CALL2(add_peer, adr, source);
	}

	void torrent_handle::force_reannounce(
		boost::posix_time::time_duration duration) const
	{
		INVARIANT_CHECK;
		TORRENT_ASYNC_CALL1(force_tracker_request, time_now() + seconds(duration.total_seconds()));
	}

#ifndef TORRENT_DISABLE_DHT
	void torrent_handle::force_dht_announce() const
	{
		INVARIANT_CHECK;
		TORRENT_ASYNC_CALL(dht_announce);
	}
#endif

	void torrent_handle::force_reannounce() const
	{
		INVARIANT_CHECK;
		TORRENT_ASYNC_CALL1(force_tracker_request, time_now());
	}

	void torrent_handle::scrape_tracker() const
	{
		INVARIANT_CHECK;
		TORRENT_ASYNC_CALL(scrape_tracker);
	}

	void torrent_handle::super_seeding(bool on) const
	{
		INVARIANT_CHECK;
		TORRENT_ASYNC_CALL1(super_seeding, on);
	}

#ifndef TORRENT_DISABLE_RESOLVE_COUNTRIES
	void torrent_handle::resolve_countries(bool r)
	{
		INVARIANT_CHECK;
		TORRENT_ASYNC_CALL1(resolve_countries, r);
	}

	bool torrent_handle::resolve_countries() const
	{
		INVARIANT_CHECK;
		TORRENT_SYNC_CALL_RET(bool, false, resolving_countries);
		return r;
	}
#endif

	void torrent_handle::get_full_peer_list(std::vector<peer_list_entry>& v) const
	{
		INVARIANT_CHECK;
		TORRENT_SYNC_CALL1(get_full_peer_list, boost::ref(v));
	}

	void torrent_handle::get_peer_info(std::vector<peer_info>& v) const
	{
		INVARIANT_CHECK;
		TORRENT_SYNC_CALL1(get_peer_info, boost::ref(v));
	}

	void torrent_handle::get_download_queue(std::vector<partial_piece_info>& queue) const
	{
		INVARIANT_CHECK;
		TORRENT_SYNC_CALL1(get_download_queue, &queue);
	}

	void torrent_handle::set_piece_deadline(int index, int deadline, int flags) const
	{
		INVARIANT_CHECK;
		TORRENT_ASYNC_CALL3(set_piece_deadline, index, deadline, flags);
	}

	void torrent_handle::reset_piece_deadline(int index) const
	{
		INVARIANT_CHECK;
		TORRENT_ASYNC_CALL1(reset_piece_deadline, index);
	}

	boost::shared_ptr<torrent> torrent_handle::native_handle() const
	{
		return m_torrent.lock();
	}

	std::size_t hash_value(torrent_status const& ts)
	{
		return hash_value(ts.handle);
	}

	std::size_t hash_value(torrent_handle const& th)
	{
		// using the locked shared_ptr value as hash doesn't work
		// for expired weak_ptrs. So, we're left with a hack
		return std::size_t(*reinterpret_cast<void* const*>(&th.m_torrent));
	}

}

