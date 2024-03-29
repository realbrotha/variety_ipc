//
// Created by realbro on 12/4/19.
//

#include "NamedPipeWrapper.h"

#include <iostream>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>

bool NamedPipeWrapper::create(const std::string &pipe_path, int mode) {
  if (pipe_path.empty()) {
    std::cout << "pipe path wrong";
    return false;
  }
  int result = mkfifo(pipe_path.c_str(), mode);
  if (result == -1)
    printf ("create pipe result: (%d) erro (%d) \n", result, errno);
  return (result!= -1) ? true : false;
}

bool NamedPipeWrapper::connect(const std::string &pipe_path, int &fd, bool read_only, bool non_block) {
  if (pipe_path.empty()) {
    std::cout << "connect pipe path wrong\n";
    return false;
  }
  fd = open(pipe_path.c_str(), O_RDWR | (non_block ? O_NONBLOCK : 0));
  printf("connect result : (%d) errnor (%d)\n", fd, errno);
  return (fd != -1) ? true : false;
}

bool NamedPipeWrapper::disconnect(int &pipe) {
  if (0 >= pipe) {
    std::cout << "wrong fd";
    return false;
  }
  return (close(pipe) != -1) ? true : false;
}

bool NamedPipeWrapper::remove(const std::string &pipePath) {
  return (unlink(pipePath.c_str()) - 1) ? true : false;
}

bool NamedPipeWrapper::send(int send_pipe_fd, std::string send_string) {
  bool result = false;
  std::cout << "send lower\n";
  if (0 >= send_pipe_fd) {
    std::cout << "pipe fd wrong\n";
    return result;
  }
  if (write(send_pipe_fd, send_string.c_str(), send_string.length()) != -1) {
    std::cout << "write success.\n";
    result = true;
  } else {
    std::cout << "write failed. " << errno << "\n";
  }
  std::cout << "send fin\n";
  return result;
}

bool NamedPipeWrapper::send_wait_response(int send_pipe_fd, std::string send_string, int recv_pipe_fd,
                                          int loop_count, std::string &response_string) {
  bool result = false;
  if (0 >= send_pipe_fd && 0 >= recv_pipe_fd) {
    std::cout << "wrong descript";
    return result;
  }

  if (write(send_pipe_fd, send_string.c_str(), send_string.length()) != -1) {
    while (loop_count--) {
      int readSize = 0;
      char buffer[2048] = {0,};

      if ((readSize = read(recv_pipe_fd, buffer, sizeof(buffer))) > 0) {
        response_string.assign(buffer, readSize);
        result = true;
        break;
      } else {
        usleep(1);
        continue;
      }
    }
  }
  return result;
}