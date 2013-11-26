#ifndef TORRENT_PEER_IDOL_EXTENSION_HPP_INCLUDED
#define TORRENT_PEER_IDOL_EXTENSION_HPP_INCLUDED

#ifdef _MSC_VER
#pragma warning(push, 1)
#endif

#include <boost/shared_ptr.hpp>
#include "libtorrent/config.hpp"

#ifdef _MSC_VER
#pragma warning(pop)
#endif

namespace libtorrent
{
	struct torrent_plugin;
	class torrent;
	TORRENT_EXPORT boost::shared_ptr<torrent_plugin> create_peer_idol_plugin(torrent*, void*);
}

#endif // TORRENT_PEER_IDOL_EXTENSION_HPP_INCLUDED
