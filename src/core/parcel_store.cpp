#include "opengenesis/core/parcel_store.hpp"
#include "opengenesis/platform/filesystem.hpp"
#include "opengenesis/security/crypto.hpp"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <vector>
namespace opengenesis::core { namespace {
std::int64_t now(){return std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();}
std::string hex(std::string_view v){constexpr char h[]="0123456789abcdef";std::string o(v.size()*2,'0');for(size_t i=0;i<v.size();++i){auto c=(unsigned char)v[i];o[i*2]=h[c>>4];o[i*2+1]=h[c&15];}return o;}
unsigned char nib(char c){if(c>='0'&&c<='9')return static_cast<unsigned char>(c-'0');if(c>='a'&&c<='f')return static_cast<unsigned char>(c-'a'+10);if(c>='A'&&c<='F')return static_cast<unsigned char>(c-'A'+10);throw std::runtime_error("hex");}
std::string unhex(std::string_view v){if(v.size()%2)throw std::runtime_error("hex");std::string o(v.size()/2,'\0');for(size_t i=0;i<o.size();++i)o[i]=(char)((nib(v[i*2])<<4)|nib(v[i*2+1]));return o;}
std::vector<std::string> tabs(const std::string& l){std::vector<std::string>f;size_t s=0;for(;;){auto e=l.find('\t',s);f.push_back(l.substr(s,e==std::string::npos?e:e-s));if(e==std::string::npos)break;s=e+1;}return f;}
bool overlap(const ParcelInfo&a,std::string_view region,std::uint16_t x1,std::uint16_t y1,std::uint16_t x2,std::uint16_t y2){return a.region_id==region&&!(x2<a.x1||x1>a.x2||y2<a.y1||y1>a.y2);}
}
ParcelStore::ParcelStore(std::string path):path_(std::move(path)){reload();}
void ParcelStore::reload(){std::scoped_lock l(mutex_);load_locked();}
std::optional<ParcelInfo> ParcelStore::create(std::string owner,std::string region,std::string name,std::uint16_t x1,std::uint16_t y1,std::uint16_t x2,std::uint16_t y2,std::string& reason){if(owner.empty()||region.empty()||name.empty()||x1>x2||y1>y2){reason="invalid-parcel";return std::nullopt;}std::scoped_lock l(mutex_);for(auto&[_,p]:parcels_)if(overlap(p,region,x1,y1,x2,y2)){reason="parcel-overlap";return std::nullopt;}auto t=now();ParcelInfo p{.id=security::random_hex(16),.region_id=std::move(region),.name=std::move(name),.owner_user_id=std::move(owner),.group_id={},.x1=x1,.y1=y1,.x2=x2,.y2=y2,.created_unix=t,.updated_unix=t};parcels_[p.id]=p;persist_locked();reason.clear();return p;}
bool ParcelStore::update_policy(std::string_view actor,std::string_view id,std::string group,bool pe,bool pb,bool gb,bool gt,std::string& reason){std::scoped_lock l(mutex_);auto it=parcels_.find(std::string{id});if(it==parcels_.end()){reason="parcel-not-found";return false;}if(it->second.owner_user_id!=actor){reason="permission-denied";return false;}it->second.group_id=std::move(group);it->second.public_entry=pe;it->second.public_build=pb;it->second.group_build=gb;it->second.group_terraform=gt;it->second.updated_unix=now();persist_locked();reason.clear();return true;}
std::optional<ParcelInfo> ParcelStore::find(std::string_view id)const{std::scoped_lock l(mutex_);auto it=parcels_.find(std::string{id});return it==parcels_.end()?std::nullopt:std::optional<ParcelInfo>{it->second};}
std::optional<ParcelInfo> ParcelStore::at(std::string_view region,double x,double y)const{if(!std::isfinite(x)||!std::isfinite(y))return std::nullopt;std::scoped_lock l(mutex_);for(auto&[_,p]:parcels_)if(p.region_id==region&&x>=p.x1&&x<=p.x2&&y>=p.y1&&y<=p.y2)return p;return std::nullopt;}
std::vector<ParcelInfo> ParcelStore::list_region(std::string_view r)const{std::scoped_lock l(mutex_);std::vector<ParcelInfo>o;for(auto&[_,p]:parcels_)if(p.region_id==r)o.push_back(p);std::sort(o.begin(),o.end(),[](auto&a,auto&b){return a.created_unix<b.created_unix;});return o;}
std::vector<ParcelInfo> ParcelStore::list_owner(std::string_view u)const{std::scoped_lock l(mutex_);std::vector<ParcelInfo>o;for(auto&[_,p]:parcels_)if(p.owner_user_id==u)o.push_back(p);return o;}
std::size_t ParcelStore::count()const{std::scoped_lock l(mutex_);return parcels_.size();}
bool ParcelStore::member_of(std::string_view g,const std::vector<std::string>& groups){return !g.empty()&&std::find(groups.begin(),groups.end(),g)!=groups.end();}
bool ParcelStore::can_enter(std::string_view r,double x,double y,std::string_view u,const std::vector<std::string>&g)const{auto p=at(r,x,y);return !p||p->public_entry||p->owner_user_id==u||member_of(p->group_id,g);}
bool ParcelStore::can_build(std::string_view r,double x,double y,std::string_view u,const std::vector<std::string>&g)const{auto p=at(r,x,y);return !p||p->public_build||p->owner_user_id==u||(p->group_build&&member_of(p->group_id,g));}
bool ParcelStore::can_terraform(std::string_view r,double x,double y,std::string_view u,const std::vector<std::string>&g)const{auto p=at(r,x,y);return !p||p->owner_user_id==u||(p->group_terraform&&member_of(p->group_id,g));}
void ParcelStore::load_locked(){parcels_.clear();std::ifstream in(path_);if(!in)return;std::string line;while(std::getline(in,line)){if(line.empty()||line[0]=='#')continue;auto f=tabs(line);if(f.size()!=15 && f.size()!=16)continue;try{ParcelInfo p{.id=f[0],.region_id=f[1],.name=unhex(f[2]),.owner_user_id=f[3],.group_id=f[4],.x1=static_cast<std::uint16_t>(std::stoul(f[5])),.y1=static_cast<std::uint16_t>(std::stoul(f[6])),.x2=static_cast<std::uint16_t>(std::stoul(f[7])),.y2=static_cast<std::uint16_t>(std::stoul(f[8])),.public_entry=f[9]=="1",.public_build=f[10]=="1",.group_build=f[11]=="1",.group_terraform=f[12]=="1",.created_unix=std::stoll(f[13]),.updated_unix=std::stoll(f[14])};parcels_[p.id]=p;}catch(...){}}}
void ParcelStore::persist_locked()const{std::filesystem::path p(path_);if(p.has_parent_path())std::filesystem::create_directories(p.parent_path());auto t=p.string()+".tmp";std::ofstream out(t,std::ios::trunc);if(!out)throw std::runtime_error("cannot write parcels");out<<"# OpenGenesisLINK parcels v1\n";for(auto&[_,x]:parcels_)out<<x.id<<'\t'<<x.region_id<<'\t'<<hex(x.name)<<'\t'<<x.owner_user_id<<'\t'<<x.group_id<<'\t'<<x.x1<<'\t'<<x.y1<<'\t'<<x.x2<<'\t'<<x.y2<<'\t'<<(x.public_entry?1:0)<<'\t'<<(x.public_build?1:0)<<'\t'<<(x.group_build?1:0)<<'\t'<<(x.group_terraform?1:0)<<'\t'<<x.created_unix<<'\t'<<x.updated_unix<<'\n';out.close();if(!out)throw std::runtime_error("cannot flush parcels");opengenesis::platform::replace_file(t,p);}
}
