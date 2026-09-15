#include "opengenesis/world/region_runtime.hpp"
#include <algorithm>
namespace opengenesis::world {
RegionRuntime::RegionRuntime(std::string id,const double hz):id_(std::move(id)),target_hz_(std::clamp(hz,1.0,240.0)){} RegionRuntime::~RegionRuntime(){stop();}
void RegionRuntime::start(){if(running_.exchange(true))return;thread_=std::thread(&RegionRuntime::loop,this);}void RegionRuntime::stop(){if(!running_.exchange(false))return;if(thread_.joinable())thread_.join();}
std::uint64_t RegionRuntime::spawn_entity(std::string name,const bool avatar,const bool physical){std::scoped_lock lock(mutex_);const auto id=next_entity_++;Entity e{id,std::move(name),avatar,0};if(physical)e.physics_body=physics_.add_body({.position={0,0,10}});entities_[id]=e;return id;}
bool RegionRuntime::remove_entity(const std::uint64_t id){std::scoped_lock lock(mutex_);const auto it=entities_.find(id);if(it==entities_.end())return false;if(it->second.physics_body)physics_.remove_body(it->second.physics_body);entities_.erase(it);return true;}
RuntimeMetrics RegionRuntime::metrics()const{std::scoped_lock lock(mutex_);std::uint64_t avatars=0;for(const auto& [_,e]:entities_)if(e.avatar)++avatars;return {ticks_.load(),entities_.size(),avatars,physics_.body_count(),sim_fps_.load()};}
void RegionRuntime::loop(){using clock=std::chrono::steady_clock;const auto step=std::chrono::duration<double>(1.0/target_hz_);auto next=clock::now();auto fps_start=next;std::uint64_t fps_ticks=0;while(running_){next+=std::chrono::duration_cast<clock::duration>(step);physics_.step(step.count());++ticks_;++fps_ticks;const auto now=clock::now();if(now-fps_start>=std::chrono::seconds(1)){const auto secs=std::chrono::duration<double>(now-fps_start).count();sim_fps_.store(static_cast<double>(fps_ticks)/secs);fps_ticks=0;fps_start=now;}std::this_thread::sleep_until(next);}}
}
