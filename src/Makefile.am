lib_LTLIBRARIES = libtorrent-rasterbar.la

if ENABLE_DHT
KADEMLIA_SOURCES = \
  kademlia/dht_tracker.cpp      \
  kademlia/find_data.cpp        \
  kademlia/node.cpp             \
  kademlia/node_id.cpp          \
  kademlia/refresh.cpp          \
  kademlia/routing_table.cpp    \
  kademlia/rpc_manager.cpp      \
  kademlia/traversal_algorithm.cpp
endif

if WITH_SHIPPED_GEOIP
GEOIP_SOURCES = GeoIP.c
endif

if WITH_OPENSSL
ASIO_OPENSSL_SOURCES = asio_ssl.cpp
endif

libtorrent_rasterbar_la_SOURCES = \
  web_connection_base.cpp     \
  alert.cpp                       \
  allocator.cpp                   \
  asio.cpp                        \
  assert.cpp                      \
  bandwidth_limit.cpp             \
  bandwidth_manager.cpp           \
  bandwidth_queue_entry.cpp       \
  bloom_filter.cpp                \
  broadcast_socket.cpp            \
  bt_peer_connection.cpp          \
  chained_buffer.cpp              \
  connection_queue.cpp            \
  ConvertUTF.cpp                  \
  create_torrent.cpp              \
  disk_buffer_holder.cpp          \
  disk_buffer_pool.cpp            \
  disk_io_thread.cpp              \
  entry.cpp                       \
  enum_net.cpp                    \
  error_code.cpp                  \
  escape_string.cpp               \
  file.cpp                        \
  file_pool.cpp                   \
  file_storage.cpp                \
  gzip.cpp                        \
  http_connection.cpp             \
  http_parser.cpp                 \
  http_seed_connection.cpp        \
  http_stream.cpp                 \
  http_tracker_connection.cpp     \
  i2p_stream.cpp                  \
  identify_client.cpp             \
  instantiate_connection.cpp      \
  ip_filter.cpp                   \
  lazy_bdecode.cpp                \
  logger.cpp                      \
  lsd.cpp                         \
  lt_trackers.cpp                 \
  magnet_uri.cpp                  \
  metadata_transfer.cpp           \
  mpi.c                           \
  natpmp.cpp                      \
  parse_url.cpp                   \
  pe_crypto.cpp                   \
  peer_connection.cpp             \
  piece_picker.cpp                \
  packet_buffer.cpp               \
  policy.cpp                      \
  puff.cpp                        \
  random.cpp                      \
  rsa.cpp                         \
  rss.cpp                         \
  session.cpp                     \
  session_impl.cpp                \
  settings.cpp                    \
  sha1.cpp                        \
  smart_ban.cpp                   \
  socket_io.cpp                   \
  socket_type.cpp                 \
  socks5_stream.cpp               \
  stat.cpp                        \
  storage.cpp                     \
  string_util.cpp                 \
  thread.cpp                      \
  torrent.cpp                     \
  torrent_handle.cpp              \
  torrent_info.cpp                \
  time.cpp                        \
  timestamp_history.cpp           \
  tracker_manager.cpp             \
  udp_socket.cpp                  \
  udp_tracker_connection.cpp      \
  upnp.cpp                        \
  ut_metadata.cpp                 \
  ut_pex.cpp                      \
  peer_idol_extension.cpp         \
  utf8.cpp                        \
  utp_socket_manager.cpp          \
  utp_stream.cpp                  \
  web_peer_connection.cpp         \
  \
  $(KADEMLIA_SOURCES)             \
  $(GEOIP_SOURCES)                \
  $(ASIO_OPENSSL_SOURCES)

#libtorrent_rasterbar_la_LDFLAGS = $(LDFLAGS) -version-info $(INTERFACE_VERSION_INFO)
libtorrent_rasterbar_la_LDFLAGS = -version-info $(INTERFACE_VERSION_INFO)

#libtorrent_rasterbar_la_LIBADD =  @BOOST_SYSTEM_LIB@ @BOOST_FILESYSTEM_LIB@ @BOOST_THREAD_LIB@ @OPENSSL_LIBS@
libtorrent_rasterbar_la_LIBADD = @BOOST_SYSTEM_LIB@ @OPENSSL_LIBS@

#AM_CXXFLAGS= -ftemplate-depth-100 -I$(top_srcdir)/include @DEBUGFLAGS@ @OPENSSL_INCLUDES@
#AM_CPPFLAGS = -ftemplate-depth-100 -I$(top_srcdir)/include @DEBUGFLAGS@ @OPENSSL_INCLUDES@
AM_CPPFLAGS = -ftemplate-depth-100 -I$(top_srcdir)/include @DEBUGFLAGS@ @OPENSSL_INCLUDES@

#AM_CFLAGS= -I$(top_srcdir)/include @DEBUGFLAGS@
#AM_LDFLAGS = $(LDFLAGS) @BOOST_SYSTEM_LIB@ @BOOST_FILESYSTEM_LIB@ @BOOST_THREAD_LIB@ @OPENSSL_LDFLAGS@ @OPENSSL_LIBS@
AM_LDFLAGS = @OPENSSL_LDFLAGS@

