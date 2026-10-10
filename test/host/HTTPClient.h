#pragma once
#include "WiFiClientSecure.h"
#include <cassert>
constexpr int HTTPC_DISABLE_FOLLOW_REDIRECTS=0;
struct HttpResponse{int status=200;std::vector<uint8_t>body;std::map<std::string,String>headers;int declaredSize=-1;};
inline std::map<std::string,HttpResponse>&httpResponses(){static std::map<std::string,HttpResponse>r;return r;}
inline std::vector<std::string>&httpUrls(){static std::vector<std::string>u;return u;}
class HTTPClient{
 public:
  WiFiClientSecure*client=nullptr;HttpResponse response;
  void setConnectTimeout(int){}void setTimeout(int){}void setReuse(bool){}void useHTTP10(bool){}void setFollowRedirects(int){}
  bool begin(WiFiClientSecure&tls,const char*url){assert(tls.verified);client=&tls;httpUrls().push_back(url);auto i=httpResponses().find(url);response=i==httpResponses().end()?HttpResponse():i->second;if(i==httpResponses().end())response.status=404;return true;}
  bool begin(WiFiClientSecure&tls,const String&url){return begin(tls,url.c_str());}
  void setUserAgent(const char*){}void addHeader(const char*,const char*){}void collectHeaders(const char**,size_t){}
  int GET(){client->data=response.body;client->cursor=0;client->live=true;return response.status;}String header(const char*key){return response.headers[key];}
  int getSize(){return response.declaredSize<0?int(response.body.size()):response.declaredSize;}
  String getString(){return std::string(response.body.begin(),response.body.end());}WiFiClient*getStreamPtr(){return client;}void end(){}
};
