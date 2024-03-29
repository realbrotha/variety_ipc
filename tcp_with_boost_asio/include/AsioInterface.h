//
// Created by realbro on 1/20/20.
//

#ifndef TESTIPC_TCP_WITH_BOOST_ASIO_INCLUDE_ASIOINTERFACE_H_
#define TESTIPC_TCP_WITH_BOOST_ASIO_INCLUDE_ASIOINTERFACE_H_

#include "AsioFactory.h"
#include "AsioServer.h"
#include "AsioClient.h"

enum INTERFACE_TYPE {
  CLIENT = 1,
  SERVER = 2
};

class AsioInterface : public AsioFactory {
 public :
  AsioInterface();
  virtual ~AsioInterface();

  bool GetInterfaceObject(unsigned int type, AsioInterface **out_interface_ptr);
};

#endif //TESTIPC_TCP_WITH_BOOST_ASIO_INCLUDE_ASIOINTERFACE_H_
