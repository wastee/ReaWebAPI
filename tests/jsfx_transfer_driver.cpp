#include "runtime/native_stream.hpp"
#include "runtime/jsfx_transfer.hpp"
#include <iostream>
using namespace reaweb;
int main() {try {
  StreamHub hub;auto budget=std::make_shared<JsfxTransfer::Budget>();
  auto transfer=std::make_shared<JsfxTransfer>(budget,std::string(64,'a'),262151,true);
  std::cout<<hub.attach_jsfx_resource(transfer,7,1,"http://jsfx-probe.test").dump()<<std::endl;
  std::string checksum;std::getline(std::cin,checksum);
  const auto bytes=transfer->seal(checksum);
  std::cout<<Json{{"bytes",bytes->size()},{"sha256",JsfxTransfer::sha256(*bytes)}}.dump()<<std::endl;
  transfer->close();hub.detach_window(7);
  return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<std::endl;return 1;}}
