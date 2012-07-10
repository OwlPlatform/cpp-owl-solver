/*
 * Copyright (c) 2012 Bernhard Firner and Rutgers University
 * All rights reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA
 * or visit http://www.gnu.org/licenses/gpl-2.0.html
 */

/*******************************************************************************
 * This file defines a class that simplifies connecting to any number of
 * aggregators as a solver.
 ******************************************************************************/

#include "solver_aggregator_connection.hpp"

#include <algorithm>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

#include <unistd.h>

#include <sys/types.h>
#include <sys/socket.h>

#include <owl/message_receiver.hpp>
#include <owl/simple_sockets.hpp>
#include <owl/aggregator_solver_protocol.hpp>
using aggregator_solver::Subscription;

void grailAggregatorThread(uint32_t port, std::string ip, std::vector<Subscription>& subscriptions,
    std::function<void (SampleData&)> packCallback, std::mutex& callback_mutex,
    SolverAggregator::interrupt_type& interrupted) {

  //std::cerr<<"Starting aggregator thread\n";
  while (SolverAggregator::interrupt_type::close_connection != interrupted) {
    //std::cerr<<"Entering aggregator thread loop\n";
    try {
      ClientSocket cs(AF_INET, SOCK_STREAM, 0, port, ip);
      if (cs) {
        //std::cerr<<"Connected to the GRAIL aggregator.\n";

        //Try to get the handshake message
        {
          std::vector<unsigned char> handshake = aggregator_solver::makeHandshakeMsg();

          //Send the handshake message
          cs.send(handshake);
          std::vector<unsigned char> raw_message(handshake.size());
          size_t length = cs.receive(raw_message);

          //Check if the handshake message failed
          if (not (length == handshake.size() and
                std::equal(handshake.begin(), handshake.end(), raw_message.begin()) )) {
            std::cerr<<"Received handshake message of length "<<length<<'\n';
            std::cerr<<"Expecting handshake of length "<<handshake.size()<<'\n';
            std::cerr<<"Failure during handshake with aggregator on "<<ip<<":"<<port<<".\n";
            return;
          }
          else {
            std::cerr<<"Successfully connected to aggregator on "<<ip<<":"<<port<<'\n';
          }
        }

        //Make a grail socket server to simplify packet handling here
        MessageReceiver s(cs);

        //Send requests that the caller specified
        //Remember how many subscriptions are sent so that we can respond to
        //add_subscription interrupt messages.
        int sent_subscriptions = 0;
        for (auto sub = subscriptions.begin(); sub != subscriptions.end(); ++sub) {
          //Send a request message
          std::vector<unsigned char> req_buff = makeSubscribeReqMsg(*sub);
          cs.send(req_buff);
          ++sent_subscriptions;
        }

        while (interrupted != SolverAggregator::interrupt_type::close_connection) {
          //Now receive incoming messages from the server.
          std::vector<unsigned char> raw_message = s.getNextMessage((bool&)interrupted);

          //If the message is long enough to actually be a message check its type
          if ( raw_message.size() > 4 ) {
            //Handle the message according to its message type.
            if ( aggregator_solver::subscription_response == raw_message[4] ) {
              std::cerr<<"Got subscription response from "<<ip<<":"<<port<<"!\n";
              Subscription sub = aggregator_solver::decodeSubscribeMsg(raw_message, raw_message.size());
              //TODO check for changes in the subscription made by the server
            } else if ( aggregator_solver::server_sample == raw_message[4] ) {
              //std::cerr<<"Got sample!\n";
              SampleData sample = aggregator_solver::decodeSampleMsg(raw_message, raw_message.size());
              if ( sample.valid ) {
                {
                  std::unique_lock<std::mutex> lck(callback_mutex);
                  packCallback(sample);
                }
              } else {
                //std::cerr<<"But the sample wasn't valid!\n";
              }
            }
          }
          //Send new subscriptions if the controller has new requests
          if (interrupted == SolverAggregator::interrupt_type::add_subscriptions) {
            for (auto sub = subscriptions.begin()+sent_subscriptions; sub != subscriptions.end(); ++sub) {
              //Send a request message
              std::vector<unsigned char> req_buff = makeSubscribeReqMsg(*sub);
              cs.send(req_buff);
              ++sent_subscriptions;
            }
            interrupted = SolverAggregator::interrupt_type::none;
          }
        }
      }
    } catch (std::exception& err) {
      std::cerr<<"Error in grail aggregator connection: "<<err.what()<<'\n';
    }
    std::cerr<<"Error in aggregator connection, waiting 1 second to retry...\n";

    //Sleep for one second, then try connecting to the server again.
    sleep(1);
  }
  //std::cerr<<"Leaving aggregator thread\n";
}


/**
 * Connect to the GRAIL servers at the specified ips and ports and
 * subscribe to any regions with a subscription in the provided
 * subscriptions vector.
 */
void grailAggregatorConnect(const std::vector<SolverAggregator::NetTarget>& servers,
    std::vector<Subscription> subscriptions, std::function<void (SampleData&)> packCallback) {

  std::mutex callback_mutex;
  std::vector<std::thread> server_threads;

  SolverAggregator::interrupt_type interrupted = SolverAggregator::interrupt_type::none;
  for (auto server = servers.begin(); server != servers.end(); ++server) {
    try {
      server_threads.push_back(std::thread(grailAggregatorThread, server->port,
            server->ip, std::ref(subscriptions), packCallback, std::ref(callback_mutex), std::ref(interrupted)));
    }
    catch (std::system_error& err) {
      std::cerr<<"Error in grail aggregator connection: "<<err.code().message()<<
        " while connecting to "<<server->ip<<':'<<server->port<<'\n';
    }
    catch (std::runtime_error& err) {
      std::cerr<<"Error in grail aggregator connection: "<<err.what()<<
        " while connecting to "<<server->ip<<':'<<server->port<<'\n';
    }
  }

  while (1) {
    //TODO Provide an interrupt mechanism
    usleep(1000);
  }
}

SolverAggregator::SolverAggregator(const std::vector<NetTarget>& servers,
    std::function<void (SampleData&)> packCallback) : servers(servers), packCallback(packCallback) {
  interrupted = interrupt_type::none;
  //Don't establish connections until rules are provided from a call to update rules.
}

SolverAggregator::~SolverAggregator() {
  //Interrupted the aggregator connections and join the threads
  disconnect();
}

void SolverAggregator::disconnect() {
  //Interrupted the aggregator connections and join the threads
  interrupted = interrupt_type::close_connection;
  for (std::thread& t : server_threads) {
    try {
      t.join();
    }
    catch (std::runtime_error err) {
      //Thread is not joinable
    }
  }
  server_threads.clear();
}

void SolverAggregator::addRules(aggregator_solver::Subscription subscription) {
  //Update the local copy of subscriptions
  {
    std::unique_lock<std::mutex> lck(sub_mutex);
    this->subscriptions.push_back(subscription);
  }
  //If addRules is called but there are no open connections then connect
  //for the first time
  if (server_threads.empty()) {
    interrupted = interrupt_type::none;
    for (auto server = servers.begin(); server != servers.end(); ++server) {
      try {
        server_threads.push_back(std::thread(grailAggregatorThread, server->port,
              server->ip, std::ref(this->subscriptions), packCallback, std::ref(callback_mutex), std::ref(interrupted)));
      }
      catch (std::system_error& err) {
        std::cerr<<"Error in grail aggregator connection: "<<err.code().message()<<
          " while connecting to "<<server->ip<<':'<<server->port<<'\n';
      }
      catch (std::runtime_error& err) {
        std::cerr<<"Error in grail aggregator connection: "<<err.what()<<
          " while connecting to "<<server->ip<<':'<<server->port<<'\n';
      }
    }
  }
  //Otherwise use the add_subscriptions interrupt to add new subscriptions
  else {
    interrupted = interrupt_type::add_subscriptions;
  }
}

void SolverAggregator::updateRules(aggregator_solver::Subscription subscription) {
  {
    std::unique_lock<std::mutex> lck(sub_mutex);
    //Replace old subscriptions with these new ones
    this->subscriptions = std::vector<aggregator_solver::Subscription>{subscription};
  }
  //Reconnect to all of the aggregators

  //Interrupted the aggregator connections, join the threads, and then reconnect
  disconnect();

  //Reset the interrupt flag
  interrupted = interrupt_type::none;
  //Reconnect
  for (auto server = servers.begin(); server != servers.end(); ++server) {
    try {
      server_threads.push_back(std::thread(grailAggregatorThread, server->port,
            server->ip, std::ref(this->subscriptions), packCallback, std::ref(callback_mutex), std::ref(interrupted)));
    }
    catch (std::system_error& err) {
      std::cerr<<"Error in grail aggregator connection: "<<err.code().message()<<
        " while connecting to "<<server->ip<<':'<<server->port<<'\n';
    }
    catch (std::runtime_error& err) {
      std::cerr<<"Error in grail aggregator connection: "<<err.what()<<
        " while connecting to "<<server->ip<<':'<<server->port<<'\n';
    }
  }
}
