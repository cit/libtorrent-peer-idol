#include "libtorrent/pch.hpp"

#ifndef TORRENT_DISABLE_EXTENSIONS

#ifdef _MSC_VER
#pragma warning(push, 1)
#endif

#include <boost/shared_ptr.hpp>
#include <boost/bind.hpp>

#ifdef _MSC_VER
#pragma warning(pop)
#endif

#include "libtorrent/peer_connection.hpp"
#include "libtorrent/bt_peer_connection.hpp"
#include "libtorrent/bencode.hpp"
#include "libtorrent/torrent.hpp"
#include "libtorrent/extensions.hpp"
#include "libtorrent/broadcast_socket.hpp"
#include "libtorrent/socket_io.hpp"
#include "libtorrent/peer_info.hpp"
#include "libtorrent/random.hpp"

#include "libtorrent/extensions/peer_idol.hpp"

#ifdef TORRENT_VERBOSE_LOGGING
#include "libtorrent/lazy_entry.hpp"
#endif

namespace libtorrent {

    class torrent;

    namespace {
        const char extension_name[] = "peer_idol";

        struct peer_idol_plugin: torrent_plugin {

            torrent& m_torrent;

            peer_idol_plugin(torrent& t) : m_torrent(t) {
#if defined TORRENT_VERBOSE_LOGGING || defined TORRENT_LOGGING || defined TORRENT_ERROR_LOGGING
                (*m_torrent.session().m_logger) << time_now_string() << " PEER_IDOL ACTIVATED (peer_idol_plugin)\n";
#endif
            }

            ~peer_idol_plugin() {}

            virtual boost::shared_ptr<peer_plugin> new_connection(peer_connection* pc);
        };


        struct peer_idol_peer_plugin : peer_plugin {
            peer_idol_peer_plugin(torrent& t, bt_peer_connection& pc)
                : m_peer_idol_extension_id(23)
                , m_torrent(t)
                , m_pc(pc)
                , m_10_second(0)
                , m_full_list(true) {

#if defined TORRENT_VERBOSE_LOGGING || defined TORRENT_LOGGING || defined TORRENT_ERROR_LOGGING
                (*m_torrent.session().m_logger) << time_now_string() << " PEER_IDOL ACTIVATED (peer_idol_peer_plugin)\n";
#endif
            }

            virtual void tick() {
                if (++m_10_second <= 10)
                    return;

#if defined TORRENT_VERBOSE_LOGGING || defined TORRENT_LOGGING || defined TORRENT_ERROR_LOGGING
                (*m_torrent.session().m_logger) << time_now_string() << " PEER_IDOL ACTIVATED (tick) " << m_pc.remote().address() << "\n";
#endif
                // only send the votes to the seeder
                if (m_pc.is_seed()) {
                    send_best_peers();
                }

                m_10_second = 0;
            }

            // can add entries to the extension handshake
            virtual void add_handshake(entry& h) {}

            void send_best_peers() {
#if defined TORRENT_VERBOSE_LOGGING || defined TORRENT_LOGGING || defined TORRENT_ERROR_LOGGING
                (*m_torrent.session().m_logger) << time_now_string() << " sending fake 23 message (idol)\n";
#endif

                // copy peers from the torrrent in a peer_connection
                // vector in order to sort it in the next step
                std::vector<peer_connection*> peers;
                for (torrent::peer_iterator i = m_torrent.begin(),
                         end(m_torrent.end()); i != end; ++i) {
                    peer_connection* peer = *i;
                    if (!peer->is_seed()) {
                        peers.push_back(peer);
                    }
                    else {
#if defined TORRENT_VERBOSE_LOGGING || defined TORRENT_LOGGING || defined TORRENT_ERROR_LOGGING
                        (*m_torrent.session().m_logger) << time_now_string() << " [peer_idol]" << " ignoring " << peer->remote() << " because it is a seeder\n";
#endif
                    }
                }

                // sort all peers according to their upload rate
                std::sort(peers.begin(), peers.end()
                          , boost::bind(&peer_connection::payload_download_compare, _1, _2));

                entry pid;
                std::string& pla = pid["added"].string();
                std::back_insert_iterator<std::string> pla_out(pla);

                // check if enough peers are available
                if (peers.size() >= 3) {

                    for (int i = 0; i < 3; ++i) {
                        tcp::endpoint  remote = peers.at(i)->remote();

                        if (remote.address().is_v4()) {
#if defined TORRENT_VERBOSE_LOGGING || defined TORRENT_LOGGING || defined TORRENT_ERROR_LOGGING
                    (*m_torrent.session().m_logger) << time_now_string() << " added peer for vote: " << remote << " (idol)\n";
#endif
                            detail::write_endpoint(remote, pla_out);
                        }

                    }

                }
                else {
#if defined TORRENT_VERBOSE_LOGGING || defined TORRENT_LOGGING || defined TORRENT_ERROR_LOGGING
                    (*m_torrent.session().m_logger) << time_now_string() << " to less peers to send a peer idol message " << peers.size() << "\n";
#endif
                }

                    std::vector<char> pid_msg;
                    bencode(std::back_inserter(pid_msg), pid);

                    char msg[6];
                    char* ptr = msg;

                    detail::write_uint32(1 + 1 + pid_msg.size(), ptr);
                    detail::write_uint8(bt_peer_connection::msg_extended, ptr);
                    detail::write_uint8(m_peer_idol_extension_id, ptr);

                    m_pc.send_buffer(msg, sizeof(msg));
                    m_pc.send_buffer(&pid_msg[0], pid_msg.size());

                }

               virtual bool on_extended(int length, int msg, buffer::const_interval body) {
                    if (msg != m_peer_idol_extension_id) return false;

                    lazy_entry pid_msg;
                    error_code ec;
                    int ret = lazy_bdecode(body.begin, body.end, pid_msg, ec);
                    if (ret != 0 || pid_msg.type() != lazy_entry::dict_t) {
#if defined TORRENT_VERBOSE_LOGGING || defined TORRENT_LOGGING || defined TORRENT_ERROR_LOGGING
                    (*m_torrent.session().m_logger) << time_now_string() << " error parsing bencoded peer vote (idol)\n";
#endif
                        return true;
                    }

                    // std::string str = pid_msg.dict_find_string_value("abc");

                    lazy_entry const* p = pid_msg.dict_find_string("added");
                    char const* in = p->string_ptr();

                    for (int i = 0; i < 3; ++i) {
                        tcp::endpoint adr = detail::read_v4_endpoint<tcp::endpoint>(in);

#if defined TORRENT_VERBOSE_LOGGING || defined TORRENT_LOGGING || defined TORRENT_ERROR_LOGGING
                        (*m_torrent.session().m_logger) << time_now_string() << " (" << m_pc.remote() << ")received fake 23 message (idol) " << adr << "\n";
#endif

                        for (torrent::peer_iterator i = m_torrent.begin(),
                         end(m_torrent.end()); i != end; ++i) {
                            peer_connection* peer = *i;
                            if (peer->remote() == adr) {
                                peer->votes++;
#if defined TORRENT_VERBOSE_LOGGING || defined TORRENT_LOGGING || defined TORRENT_ERROR_LOGGING
                                (*m_torrent.session().m_logger) << time_now_string() << " " << " we have " << adr << " with " << peer->votes << " votes in our peer list (idol)\n";
#endif
                            }
                            else {
#if defined TORRENT_VERBOSE_LOGGING || defined TORRENT_LOGGING || defined TORRENT_ERROR_LOGGING
                                (*m_torrent.session().m_logger) << time_now_string() << " " << adr << " is not in our peer list (idol)\n";
#endif
                            }
                        }

                    }
#if defined TORRENT_VERBOSE_LOGGING || defined TORRENT_LOGGING || defined TORRENT_ERROR_LOGGING
                    (*m_torrent.session().m_logger) << time_now_string() << " --------------------------- (idol)\n";
#endif

                return true;
            }

                    int m_peer_idol_extension_id;

                    typedef std::vector<std::pair<address_v4::bytes_type, boost::uint16_t> > peers4_t;
                    peers4_t m_peers;

                    torrent& m_torrent;
                    bt_peer_connection& m_pc;

                    int m_10_second;
                    bool m_full_list;
            };

                boost::shared_ptr<peer_plugin> peer_idol_plugin::new_connection(peer_connection* pc)
                {
                if (pc->type() != peer_connection::bittorrent_connection)
                    return boost::shared_ptr<peer_plugin>();

                bt_peer_connection* c = static_cast<bt_peer_connection*>(pc);
                return boost::shared_ptr<peer_plugin>(new peer_idol_peer_plugin(m_torrent, *c));
            }

            }
            }

                namespace libtorrent
                {
                boost::shared_ptr<torrent_plugin> create_peer_idol_plugin(torrent* t, void*)
                {

                return boost::shared_ptr<torrent_plugin>(new peer_idol_plugin(*t));
            }
            }


#endif
