#include "opengenesis/core/world_registry.hpp"
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>
namespace opengenesis::core {
namespace { long long epoch_ms(const std::chrono::system_clock::time_point p){return std::chrono::duration_cast<std::chrono::milliseconds>(p.time_since_epoch()).count();} std::chrono::system_clock::time_point from_ms(const long long v){return std::chrono::system_clock::time_point{std::chrono::milliseconds{v}};} }
WorldRegistry::WorldRegistry(std::string path):storage_path_(std::move(path)){load();}
WorldNodeInfo WorldRegistry::register_or_reconnect(std::string id,std::string name,std::string endpoint){if(id.empty()||name.empty())throw std::runtime_error("invalid world registration");std::scoped_lock lock(mutex_);auto& n=nodes_[id];n.id=std::move(id);n.name=std::move(name);n.endpoint=std::move(endpoint);n.state="online";++n.generation;const auto now=std::chrono::system_clock::now();if(n.registered_at.time_since_epoch().count()==0)n.registered_at=now;n.last_seen=now;persist_locked();return n;}
bool WorldRegistry::touch(const std::string& id,const std::uint64_t generation){std::scoped_lock lock(mutex_);const auto it=nodes_.find(id);if(it==nodes_.end()||it->second.generation!=generation)return false;it->second.last_seen=std::chrono::system_clock::now();it->second.state="online";persist_locked();return true;}
bool WorldRegistry::mark_offline(const std::string& id,const std::uint64_t generation){std::scoped_lock lock(mutex_);const auto it=nodes_.find(id);if(it==nodes_.end()||it->second.generation!=generation)return false;it->second.state="offline";persist_locked();return true;}
std::size_t WorldRegistry::expire_stale(const std::chrono::seconds timeout){std::scoped_lock lock(mutex_);const auto now=std::chrono::system_clock::now();std::size_t count=0;for(auto& [_,n]:nodes_)if(n.state=="online"&&now-n.last_seen>timeout){n.state="offline";++count;}if(count)persist_locked();return count;}
std::optional<WorldNodeInfo> WorldRegistry::find(const std::string& id)const{std::scoped_lock lock(mutex_);if(const auto it=nodes_.find(id);it!=nodes_.end())return it->second;return std::nullopt;}
std::vector<WorldNodeInfo> WorldRegistry::list()const{std::scoped_lock lock(mutex_);std::vector<WorldNodeInfo> out;out.reserve(nodes_.size());for(const auto& [_,n]:nodes_)out.push_back(n);return out;}
std::size_t WorldRegistry::size()const{std::scoped_lock lock(mutex_);return nodes_.size();}
void WorldRegistry::load(){if(storage_path_.empty())return;std::ifstream in(storage_path_);std::string line;while(std::getline(in,line)){std::istringstream s(line);WorldNodeInfo n;long long reg=0,last=0;s>>std::quoted(n.id)>>std::quoted(n.name)>>std::quoted(n.endpoint)>>std::quoted(n.state)>>n.generation>>reg>>last;if(!n.id.empty()){n.registered_at=from_ms(reg);n.last_seen=from_ms(last);n.state="offline";nodes_[n.id]=std::move(n);}}}
void WorldRegistry::persist_locked()const{if(storage_path_.empty())return;const std::filesystem::path p(storage_path_);if(p.has_parent_path())std::filesystem::create_directories(p.parent_path());const auto tmp=storage_path_+".tmp";std::ofstream out(tmp,std::ios::trunc);if(!out)throw std::runtime_error("cannot persist world registry");for(const auto& [_,n]:nodes_)out<<std::quoted(n.id)<<' '<<std::quoted(n.name)<<' '<<std::quoted(n.endpoint)<<' '<<std::quoted(n.state)<<' '<<n.generation<<' '<<epoch_ms(n.registered_at)<<' '<<epoch_ms(n.last_seen)<<'\n';out.close();std::filesystem::rename(tmp,storage_path_);}
}
