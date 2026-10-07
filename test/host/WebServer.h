#pragma once
#include "Arduino.h"
#include <functional>
constexpr int HTTP_GET=0,HTTP_POST=1;
class WebServer;
inline std::vector<WebServer*>& servers(){static std::vector<WebServer*>s;return s;}
class WebServer{
 public:
  explicit WebServer(int){servers().push_back(this);}bool running=false;int statusCode=0;String body;
  std::map<std::pair<std::string,int>,std::function<void()>> handlers;std::function<void()>fallback;
  std::map<std::string,String> args,headers;
  void collectHeaders(const char**,size_t){}void on(const char*p,int method,std::function<void()>f){handlers[{p,method}]=f;}void onNotFound(std::function<void()>f){fallback=f;}
  void begin(){running=true;}void stop(){running=false;}void handleClient(){}
  String arg(const char*k){return args[k];}String header(const char*k){return headers[k];}void sendHeader(const char*,const char*,bool=false){}
  void send(int code,const char*,const String&data){statusCode=code;body=data;}
  void request(const char*path,int method,std::map<std::string,String>a={},std::map<std::string,String>h={}){args=a;headers=h;auto i=handlers.find({path,method});if(i==handlers.end())fallback();else i->second();}
};
