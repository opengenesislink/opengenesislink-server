#include "opengenesis/core/landmark_store.hpp"
#include "opengenesis/platform/filesystem.hpp"
#include "opengenesis/security/crypto.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>
namespace opengenesis::core { namespace {std::int64_t now(){return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();}std::string clean(std::string v){for(char&c:v)if(c=='\t'||c=='\n'||c=='\r')c=' ';if(v.size()>128)v.resize(128);return v;}}
LandmarkStore::LandmarkStore(std::string path):path_(std::move(path)){load();}
std::optional<LandmarkInfo> LandmarkStore::create(std::string user,std::string name,std::string region,double x,double y,double z,std::string& reason){name=clean(std::move(name));if(user.empty()||name.empty()||region.empty()||!std::isfinite(x)||!std::isfinite(y)||!std::isfinite(z)||x<0||x>255||y<0||y>255){reason="invalid-landmark";return std::nullopt;}LandmarkInfo m{.id=security::random_hex(16),.user_id=std::move(user),.name=std::move(name),.region_id=std::move(region),.x=x,.y=y,.z=z,.created_unix=now()};std::scoped_lock l(mutex_);items_.push_back(m);persist_locked();reason.clear();return m;}
std::optional<LandmarkInfo> LandmarkStore::find(std::string_view id)const{std::scoped_lock l(mutex_);auto it=std::find_if(items_.begin(),items_.end(),[&](auto&m){return m.id==id;});return it==items_.end()?std::nullopt:std::optional<LandmarkInfo>{*it};}
std::vector<LandmarkInfo> LandmarkStore::list_for_user(std::string_view user)const{std::scoped_lock l(mutex_);std::vector<LandmarkInfo>o;for(auto&m:items_)if(m.user_id==user)o.push_back(m);return o;}
bool LandmarkStore::remove(std::string_view user,std::string_view id){std::scoped_lock l(mutex_);auto n=std::erase_if(items_,[&](auto&m){return m.user_id==user&&m.id==id;});if(n)persist_locked();return n>0;}
std::size_t LandmarkStore::count()const{std::scoped_lock l(mutex_);return items_.size();}
void LandmarkStore::load(){std::scoped_lock l(mutex_);items_.clear();std::ifstream in(path_);std::string line;while(std::getline(in,line)){if(line.empty()||line[0]=='#')continue;std::istringstream s(line);std::vector<std::string>f;std::string v;while(std::getline(s,v,'\t'))f.push_back(v);if(f.size()!=8)continue;try{items_.push_back({.id=f[0],.user_id=f[1],.name=f[2],.region_id=f[3],.x=std::stod(f[4]),.y=std::stod(f[5]),.z=std::stod(f[6]),.created_unix=std::stoll(f[7])});}catch(...){}}}
void LandmarkStore::persist_locked()const{std::filesystem::path p(path_);if(p.has_parent_path())std::filesystem::create_directories(p.parent_path());auto t=p.string()+".tmp";std::ofstream out(t,std::ios::trunc);if(!out)throw std::runtime_error("cannot write landmarks");out<<"# OpenGenesisLINK landmarks v1\n";for(auto&m:items_)out<<m.id<<'\t'<<m.user_id<<'\t'<<m.name<<'\t'<<m.region_id<<'\t'<<m.x<<'\t'<<m.y<<'\t'<<m.z<<'\t'<<m.created_unix<<'\n';out.close();if(!out)throw std::runtime_error("cannot flush landmarks");opengenesis::platform::replace_file(t,p);}
}
