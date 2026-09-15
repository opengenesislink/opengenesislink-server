#include "opengenesis/physics/physics_world.hpp"
#include <algorithm>
#include <stdexcept>
namespace opengenesis::physics {
std::uint64_t PhysicsWorld::add_body(Body b){std::scoped_lock lock(mutex_);if(b.id==0)b.id=next_id_++;else next_id_=std::max(next_id_,b.id+1);b.mass=std::max(0.001,b.mass);bodies_[b.id]=b;return b.id;}
bool PhysicsWorld::remove_body(const std::uint64_t id){std::scoped_lock lock(mutex_);return bodies_.erase(id)>0;}
void PhysicsWorld::step(const double dt){if(dt<=0.0)return;std::scoped_lock lock(mutex_);for(auto& [_,b]:bodies_){if(!b.dynamic)continue;b.velocity.x+=gravity_.x*dt;b.velocity.y+=gravity_.y*dt;b.velocity.z+=gravity_.z*dt;b.position.x+=b.velocity.x*dt;b.position.y+=b.velocity.y*dt;b.position.z+=b.velocity.z*dt;if(b.position.z<ground_height_){b.position.z=ground_height_;if(b.velocity.z<0)b.velocity.z=-b.velocity.z*std::clamp(b.restitution,0.0,1.0);if(b.velocity.z<0.01)b.velocity.z=0;}}}
std::size_t PhysicsWorld::body_count()const{std::scoped_lock lock(mutex_);return bodies_.size();}
Body PhysicsWorld::body(const std::uint64_t id)const{std::scoped_lock lock(mutex_);const auto it=bodies_.find(id);if(it==bodies_.end())throw std::runtime_error("unknown physics body");return it->second;}
void PhysicsWorld::set_gravity(const Vec3 g){std::scoped_lock lock(mutex_);gravity_=g;}void PhysicsWorld::set_ground_height(const double h){std::scoped_lock lock(mutex_);ground_height_=h;}
}
