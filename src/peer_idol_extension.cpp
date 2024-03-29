#include "libtorrent/pch.hpp"

#ifndef TORRENT_DISABLE_EXTENSIONS

#ifdef _MSC_VER
#pragma warning(push, 1)
#endif

#include <boost/shared_ptr.hpp>
#include <boost/bind.hpp>

#include <iomanip>

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
        // name of this extension
        const char extension_name[] = "peer_idol";

        struct peer_idol_plugin: torrent_plugin {

            torrent& m_torrent;

            peer_idol_plugin(torrent& t) : m_torrent(t) {}

            ~peer_idol_plugin() {}

            virtual boost::shared_ptr<peer_plugin> new_connection(peer_connection* pc);
        };


        struct peer_idol_peer_plugin : peer_plugin {
            peer_idol_peer_plugin(torrent& t, bt_peer_connection& pc)
                : m_peer_idol_extension_id(23)
                , m_torrent(t)
                , m_pc(pc)
                , m_15_second(15)
                , m_full_list(true) {

#if defined TORRENT_VERBOSE_LOGGING || defined TORRENT_LOGGING || defined TORRENT_ERROR_LOGGING
                (*m_torrent.session().m_logger) << time_now_string() << " [peer_idol] activated (peer_idol_peer_plugin)\n";
#endif
            }

            virtual void tick() {
                // make sure that peer idol messages only sent every 10 seconds
                if (++m_15_second <= 15)
                    return;

#if defined TORRENT_VERBOSE_LOGGING || defined TORRENT_LOGGING || defined TORRENT_ERROR_LOGGING
                (*m_torrent.session().m_logger) << time_now_string() << " [peer_idol] activated (tick) " << m_pc.remote().address() << "\n";
#endif
                // only send the votes to the seeder
                if (m_pc.is_seed() &&
                    m_torrent.settings().seed_choking_algorithm == session_settings::peer_idol) {
                    send_best_peers();
                }

                m_15_second = 0;
            }

            // can add entries to the extension handshake
            virtual void add_handshake(entry& h) {}

            void send_best_peers() {
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


                int vote_size = 0;

                if (peers.size() == 0) {
#if defined TORRENT_VERBOSE_LOGGING || defined TORRENT_LOGGING || defined TORRENT_ERROR_LOGGING
                    (*m_torrent.session().m_logger) << time_now_string() << " [peer_idol] to less peers to send a peer idol message: " << peers.size() << "\n";
#endif
                    return;
                }
                else if (peers.size() > 3) {
                    vote_size = 3;
                }
                else if (peers.size() < 3 and peers.size() > 0) {
                    vote_size = peers.size();
                }

                for (int i = 0; i < vote_size; ++i) {
                    tcp::endpoint  remote = peers.at(i)->remote();

                    // if the peer has told us which port its listening on,
                    // use that port. But only if we didn't connect to the peer.
                    // if we connected to it, use the port we know works
                    policy::peer *pi = 0;
                    if ((pi = peers.at(i)->peer_info_struct()) && pi->port > 0)
                        remote.port(pi->port);

                    if (remote.address().is_v4()) {
#if defined TORRENT_VERBOSE_LOGGING || defined TORRENT_LOGGING || defined TORRENT_ERROR_LOGGING
                        (*m_torrent.session().m_logger) << time_now_string() << " [peer_idol] added peer for vote " << i << ": " << remote << " (idol)\n";
#endif
                        detail::write_endpoint(remote, pla_out);
                    }
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

#if defined TORRENT_VERBOSE_LOGGING || defined TORRENT_LOGGING || defined TORRENT_ERROR_LOGGING
                (*m_torrent.session().m_logger) << time_now_string() << " [peer_idol]" << " send peer vote\n";
#endif

            }

            virtual bool on_extended(int length, int msg, buffer::const_interval body) {

                if (msg != m_peer_idol_extension_id) return false;
                if (body.left() < length) return true;

#if defined TORRENT_VERBOSE_LOGGING || defined TORRENT_LOGGING || defined TORRENT_ERROR_LOGGING
                (*m_torrent.session().m_logger) << time_now_string() << " [peer_idol] received peer vote"
                                                << "from : " << m_pc.remote()
                                                << ", length: " << length
                                                << ", body size:  " << body.left()
                                                << "\n";
#endif

                lazy_entry pid_msg;
                error_code ec;
                int ret = lazy_bdecode(body.begin, body.end, pid_msg, ec);

                if (ret != 0 || pid_msg.type() != lazy_entry::dict_t) {
#if defined TORRENT_VERBOSE_LOGGING || defined TORRENT_LOGGING || defined TORRENT_ERROR_LOGGING
                    (*m_torrent.session().m_logger) << time_now_string() << " [peer_idol] error parsing bencoded peer vote,"
                                                    << ", length: " << length
                                                    << ", error:  " << ec.message()
                                                    << "\n";
#endif
                    return true;
                }

                int rank_points[3];
                rank_points[0] = 3;
                rank_points[1] = 2;
                rank_points[2] = 1;

                lazy_entry const* p = pid_msg.dict_find_string("added");
                char const* in = p->string_ptr();

                tcp::endpoint votes[3];
                peer_connection* votes_valid[3]   = {NULL};
                peer_connection* votes_invalid[3] = {NULL};

                int num_peers = p->string_length() / 6;

                for (int i = 0; i < num_peers; ++i) {
                    votes[i] = detail::read_v4_endpoint<tcp::endpoint>(in);

#if defined TORRENT_VERBOSE_LOGGING || defined TORRENT_LOGGING || defined TORRENT_ERROR_LOGGING
                    (*m_torrent.session().m_logger) << time_now_string() << " [peer_idol] " << " vote contains peer: " << votes[i].address() << ":" << votes[i].port() << " \n";
#endif
                }

                int found_peers = 0;
                for (torrent::peer_iterator i = m_torrent.begin(),
                         end(m_torrent.end()); i != end; ++i) {
                    peer_connection* peer = *i;

                    for (int i = 0; i < num_peers; ++i) {
                        if (peer->remote().address() == votes[i].address()) {
                            found_peers++;
                            votes_valid[i] = peer;
                        }
                    }
                }

#if defined TORRENT_VERBOSE_LOGGING || defined TORRENT_LOGGING || defined TORRENT_ERROR_LOGGING
                (*m_torrent.session().m_logger) << time_now_string() << " [peer_idol] " << " found peers: " << found_peers << " \n";
#endif

                for (int i = 0; i < num_peers; ++i) {
                    // add points to the valid peers
                    if (votes_valid[i] != NULL) {
                        votes_valid[i]->votes += rank_points[i];
                    }
                    // add invalid peers to m_peers, since they might
                    // be future candidates
                    else {
                        peers4_t::value_type v(votes[i].address().to_v4().to_bytes(), votes[i].port());
                        peers4_t::iterator j = std::lower_bound(m_peers.begin(), m_peers.end(), v);
                        // do we already know about this peer?
                        if (j != m_peers.end() && *j == v) continue;
#if defined TORRENT_VERBOSE_LOGGING || defined TORRENT_LOGGING || defined TORRENT_ERROR_LOGGING
                        (*m_torrent.session().m_logger) << time_now_string() << " [peer_idol] " << " added " << votes[i].address() << ":" << votes[i].port() << " to m_peers\n";
#endif
                       m_peers.insert(j, v);
                       policy& p = m_torrent.get_policy();
                       peer_id pid(0);
                       p.add_peer(votes[i], pid, peer_info::pex, '0');
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

            int m_15_second;
            bool m_full_list;
        };

        boost::shared_ptr<peer_plugin> peer_idol_plugin::new_connection(peer_connection* pc) {
            if (pc->type() != peer_connection::bittorrent_connection)
                return boost::shared_ptr<peer_plugin>();

            bt_peer_connection* c = static_cast<bt_peer_connection*>(pc);
            return boost::shared_ptr<peer_plugin>(new peer_idol_peer_plugin(m_torrent, *c));
        }

    }
}

namespace libtorrent {
    boost::shared_ptr<torrent_plugin> create_peer_idol_plugin(torrent* t, void*) {

        return boost::shared_ptr<torrent_plugin>(new peer_idol_plugin(*t));
    }
}


#endif
