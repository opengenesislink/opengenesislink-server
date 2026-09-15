#include "opengenesis/core/node_sessions.hpp"
namespace opengenesis::core {
void NodeSessions::open(std::string id,const std::uint64_t gen){std::scoped_lock lock(mutex_);const auto key=id;sessions_[key]={std::move(id),gen,std::chrono::steady_clock::now()};}
bool NodeSessions::renew(const std::string& id,const std::uint64_t gen){std::scoped_lock lock(mutex_);const auto it=sessions_.find(id);if(it==sessions_.end()||it->second.generation!=gen)return false;it->second.last_lease=std::chrono::steady_clock::now();return true;}
void NodeSessions::close(const std::string& id,const std::uint64_t gen){std::scoped_lock lock(mutex_);const auto it=sessions_.find(id);if(it!=sessions_.end()&&it->second.generation==gen)sessions_.erase(it);}
std::vector<NodeSession> NodeSessions::expired(const std::chrono::seconds timeout)const{std::scoped_lock lock(mutex_);std::vector<NodeSession> out;const auto now=std::chrono::steady_clock::now();for(const auto& [_,s]:sessions_)if(now-s.last_lease>timeout)out.push_back(s);return out;}
}
