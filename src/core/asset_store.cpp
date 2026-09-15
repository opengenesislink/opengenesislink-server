#include "opengenesis/core/asset_store.hpp"

#include "opengenesis/security/crypto.hpp"

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <stdexcept>

namespace opengenesis::core {
namespace {
std::int64_t unix_now() {
    return std::chrono::duration_cast<std::chrono::seconds>(
               std::chrono::system_clock::now().time_since_epoch()).count();
}
std::string hex_text(std::string_view value) {
    constexpr char hex[] = "0123456789abcdef";
    std::string out(value.size()*2,'0');
    for (std::size_t i=0;i<value.size();++i) { auto c=static_cast<unsigned char>(value[i]); out[i*2]=hex[c>>4U]; out[i*2+1]=hex[c&15U]; }
    return out;
}
unsigned char nibble(char c) { if(c>='0'&&c<='9')return static_cast<unsigned char>(c-'0'); if(c>='a'&&c<='f')return static_cast<unsigned char>(c-'a'+10); if(c>='A'&&c<='F')return static_cast<unsigned char>(c-'A'+10); throw std::runtime_error("invalid hex"); }
std::string unhex(std::string_view v){ if(v.size()%2)throw std::runtime_error("invalid hex"); std::string o(v.size()/2,'\0'); for(std::size_t i=0;i<o.size();++i)o[i]=static_cast<char>((nibble(v[i*2])<<4U)|nibble(v[i*2+1])); return o; }
std::vector<std::string> tabs(const std::string& line){ std::vector<std::string> f; std::size_t s=0; for(;;){auto e=line.find('\t',s); f.push_back(line.substr(s,e==std::string::npos?e:e-s)); if(e==std::string::npos)break; s=e+1;} return f; }
bool valid_text(std::string_view v,std::size_t max){ return !v.empty()&&v.size()<=max&&std::none_of(v.begin(),v.end(),[](unsigned char c){return c<0x20||c==0x7f;}); }
}

AssetStore::AssetStore(std::string metadata_path, std::filesystem::path blob_directory, std::size_t max_asset_bytes)
    : metadata_path_(std::move(metadata_path)), blob_directory_(std::move(blob_directory)), max_asset_bytes_(std::clamp<std::size_t>(max_asset_bytes,1024,16U*1024U*1024U)) { load(); }

std::optional<AssetInfo> AssetStore::create(std::string owner_user_id, std::string name, std::string mime_type,
                                             std::string data, std::string& reason) {
    if(owner_user_id.empty()){reason="invalid-owner";return std::nullopt;}
    if(!valid_text(name,128)){reason="invalid-name";return std::nullopt;}
    if(!valid_text(mime_type,96)){reason="invalid-mime-type";return std::nullopt;}
    if(data.empty()||data.size()>max_asset_bytes_){reason="invalid-asset-size";return std::nullopt;}
    AssetInfo info{.id=security::random_hex(16),.owner_user_id=std::move(owner_user_id),.name=std::move(name),
                   .mime_type=std::move(mime_type),.content_hash=security::sha256_hex(data),
                   .size=static_cast<std::uint64_t>(data.size()),.created_unix=unix_now()};
    std::scoped_lock lock(mutex_);
    std::filesystem::create_directories(blob_directory_);
    const auto blob=blob_directory_/info.content_hash;
    if(!std::filesystem::exists(blob)){
        const auto tmp=blob.string()+".tmp";
        std::ofstream out(tmp,std::ios::binary|std::ios::trunc); if(!out){reason="blob-write-failed";return std::nullopt;}
        out.write(data.data(),static_cast<std::streamsize>(data.size())); out.close(); if(!out){reason="blob-write-failed";return std::nullopt;}
        std::filesystem::rename(tmp,blob);
    }
    assets_[info.id]=info; persist_locked(); reason.clear(); return info;
}
std::optional<AssetInfo> AssetStore::find(std::string_view id) const { std::scoped_lock lock(mutex_); auto it=assets_.find(std::string{id}); return it==assets_.end()?std::nullopt:std::optional<AssetInfo>{it->second}; }
std::optional<std::string> AssetStore::read(std::string_view id,std::string_view owner) const {
    AssetInfo info; {std::scoped_lock lock(mutex_); auto it=assets_.find(std::string{id}); if(it==assets_.end()||it->second.owner_user_id!=owner)return std::nullopt; info=it->second;}
    std::ifstream in(blob_directory_/info.content_hash,std::ios::binary); if(!in)return std::nullopt; std::string data((std::istreambuf_iterator<char>(in)),{}); if(data.size()!=info.size)return std::nullopt; return data;
}
std::vector<AssetInfo> AssetStore::list_for_user(std::string_view owner) const { std::scoped_lock lock(mutex_); std::vector<AssetInfo> out; for(auto& [_,a]:assets_)if(a.owner_user_id==owner)out.push_back(a); std::sort(out.begin(),out.end(),[](auto&a,auto&b){return a.created_unix<b.created_unix;}); return out; }
std::size_t AssetStore::count() const {std::scoped_lock lock(mutex_);return assets_.size();}
std::uint64_t AssetStore::total_bytes() const {std::scoped_lock lock(mutex_);std::uint64_t n=0;for(auto&[_,a]:assets_)n+=a.size;return n;}
void AssetStore::load(){ std::scoped_lock lock(mutex_); assets_.clear(); std::ifstream in(metadata_path_); if(!in)return; std::string line; while(std::getline(in,line)){if(line.empty()||line[0]=='#')continue;auto f=tabs(line);if(f.size()!=7)continue;try{AssetInfo a{.id=f[0],.owner_user_id=f[1],.name=unhex(f[2]),.mime_type=unhex(f[3]),.content_hash=f[4],.size=std::stoull(f[5]),.created_unix=std::stoll(f[6])};if(!a.id.empty()&&!a.owner_user_id.empty())assets_[a.id]=std::move(a);}catch(...){}} }
void AssetStore::persist_locked() const { std::filesystem::path p(metadata_path_);if(p.has_parent_path())std::filesystem::create_directories(p.parent_path());auto tmp=p.string()+".tmp";std::ofstream out(tmp,std::ios::trunc);if(!out)throw std::runtime_error("cannot write asset metadata");out<<"# OpenGenesisLINK asset metadata v1\n";for(auto&[_,a]:assets_)out<<a.id<<'\t'<<a.owner_user_id<<'\t'<<hex_text(a.name)<<'\t'<<hex_text(a.mime_type)<<'\t'<<a.content_hash<<'\t'<<a.size<<'\t'<<a.created_unix<<'\n';out.close();if(!out)throw std::runtime_error("cannot flush asset metadata");std::filesystem::rename(tmp,p); }
} // namespace opengenesis::core
