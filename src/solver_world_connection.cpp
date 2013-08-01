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
 * This file defines a class that simplifies connecting to the world model
 * as a solver.
 ******************************************************************************/

#include "solver_world_connection.hpp"

#include <algorithm>
#include <iostream>
#include <string>
#include <tuple>
#include <unistd.h>
#include <vector>

#include <sys/types.h>
#include <sys/socket.h>

//Send a handshake and a type declaration message.
bool SolverWorldModel::reconnect() {
  if (s) {
    std::cout<<"Connected to the GRAIL world model.\n";
  } else {
    //Otherwise try to make a new connection
    ClientSocket s2(AF_INET, SOCK_STREAM, 0, port, ip);
    if (not s2) {
      std::cerr<<"Failed to connect to the GRAIL world model.\n";
      return false;
    }
    else {
      s = std::move(s2);
    }
  }

  //Try to get the handshake message
  {
    std::vector<unsigned char> handshake = world_model::solver::makeHandshakeMsg();

    //Send the handshake message
    s.send(handshake);
    std::vector<unsigned char> raw_message(handshake.size());
    size_t length = s.receive(raw_message);

    //Check if the handshake message failed
    if (not (length == handshake.size() and
          std::equal(handshake.begin(), handshake.end(), raw_message.begin()) )) {
      std::cerr<<"Failure during solver handshake with world model.\n";
      return false;
    }
  }

  //Send the type announcement message
  try {
    s.send(world_model::solver::makeTypeAnnounceMsg(types, origin));
  }
  catch (std::runtime_error err) {
    std::cerr<<"Problem sending type announce message: "<<err.what()<<'\n';
    return false;
  }

  //Clear the old thread if one was running
  if (running) {
    interrupted = true;
    on_demand_tracker.join();
  }

  ss.previous_unfinished.clear();
  running = true;
  interrupted = false;
  //Start the on_demand status tracking thread
  on_demand_tracker = std::thread(std::mem_fun(&SolverWorldModel::trackOnDemands), this);
  return true;
}

void SolverWorldModel::sendAndReconnect(const std::vector<unsigned char>& buff) {
  bool sent = false;
  bool first_wait = true;
  int wait_time = 1;
  while (not sent) {
    if (not first_wait) {
      //std::cerr<<"Sleeping for "<<wait_time<<" seconds\n";
      sleep(wait_time);
      wait_time = 8;
    }
    //std::cerr<<"Checking connection\n";
    if (not s) {
      //std::cerr<<"Trying to reconnect\n";
      reconnect();
    }
    else {
      try {
        s.send(buff);
        sent = true;
      }
      catch (std::runtime_error& err) {
        std::cerr<<"Problem with solver world model connection: "<<err.what()<<'\n';
      }
    }
    first_wait = false;
  }
}

static std::string toString(const std::u16string& str) {
  return std::string(str.begin(), str.end());
}

void SolverWorldModel::trackOnDemands() {
  using world_model::solver::MessageID;
  //Continue processing packets while the connection is open
  try {
    while (not interrupted) {
      std::vector<unsigned char> in_buff = ss.getNextMessage(interrupted);
      if (5 <= in_buff.size()) {
        MessageID message_type = (MessageID)in_buff[4];
        if (message_type == MessageID::start_on_demand) {
          std::vector<std::tuple<uint32_t, std::vector<std::u16string>>> trans =
            world_model::solver::decodeStartOnDemand(in_buff);
          std::unique_lock<std::mutex> lck(trans_mutex);
          for (auto I = trans.begin(); I != trans.end(); ++I) {
            std::cerr<<"OnDemand "<<std::get<0>(*I)<<" has "<<std::get<1>(*I).size()<<" URI requests.\n";
            std::vector<std::u16string>& requests = std::get<1>(*I);
            for (std::u16string& request : requests) {
              //Store the regex pattern sent by the world model.
              std::cerr<<"Enabling on_demand: "<<std::get<0>(*I)<<" with string "<<toString(request)<<'\n';

              OnDemandArgs ta;
              ta.request = request;
              int err = regcomp(&ta.exp, toString(ta.request).c_str(), REG_EXTENDED);
              if (0 != err) {
                ta.valid = false;
                std::cerr<<"Error compiling regular expression "<<toString(ta.request)<<" in on_demand request to solver client.\n";
              }
              else {
                ta.valid = true;
              }
              on_demand_on[std::get<0>(*I)].insert(ta);
            }
          }
        }
        else if (message_type == MessageID::stop_on_demand) {
          std::vector<std::tuple<uint32_t, std::vector<std::u16string>>> trans =
            world_model::solver::decodeStopOnDemand(in_buff);
          std::unique_lock<std::mutex> lck(trans_mutex);
          for (auto I = trans.begin(); I != trans.end(); ++I) {
            uint32_t attr_name = std::get<0>(*I);
            std::vector<std::u16string>& requests = std::get<1>(*I);
            for (std::u16string& request : requests) {
              //Remove the regex that was sent by the world model
              std::cerr<<"Disabling on_demand: "<<attr_name<<" with request "<<toString(request)<<'\n';
              if (on_demand_on.end() != on_demand_on.find(attr_name)) {
                std::multiset<OnDemandArgs>& uri_set = on_demand_on[attr_name];
                auto J = std::find_if(uri_set.begin(), uri_set.end(),
                    [&](const OnDemandArgs& ta) { return ta.request == request;});
                if (J != uri_set.end()) {
                  OnDemandArgs ta = *J;
                  if (ta.valid) {
                    regfree(&ta.exp);
                  }
                  uri_set.erase(J);
                }
              }
            }
          }
        }
        else if (message_type == MessageID::keep_alive) {
          //Send a keep alive message in reply to a keep alive from
          //the server. This makes sure that we are replying at less
          //than the sever's timeout period.
          std::unique_lock<std::mutex> lck(send_mutex);
          sendAndReconnect(world_model::solver::makeKeepAlive());
        }
      }
      else {
        std::cerr<<"Got an invalid sized message (size = "<<in_buff.size()<<'\n';
      }
    }
  }
  catch (std::exception& e) {
    std::cerr<<"Error with solver connection: "<<e.what()<<'\n';
    return;
  }
}

SolverWorldModel::SolverWorldModel(std::string ip, uint16_t port, std::vector<std::pair<std::u16string, bool>>& types, std::u16string origin) : s(AF_INET, SOCK_STREAM, 0, port, ip), ss(s) {
  running = false;
  this->origin = origin;
  //Store the alias types that this solver will use
  for (auto I = types.begin(); I != types.end(); ++I) {
    world_model::solver::AliasType at{(uint32_t)(this->types.size()+1), I->first, I->second};
    this->types.push_back(at);
    aliases[at.type] = at.alias;
    if (I->second) {
      if (on_demand_on.end() == on_demand_on.find(at.alias)) {
        on_demand_on[at.alias] = std::multiset<OnDemandArgs>();
      }
    }
  }
  //Store these values so that we can reconnect later
  this->ip = ip;
  this->port = port;

  reconnect();
}

SolverWorldModel::~SolverWorldModel() {
  if (running) {
    interrupted = true;
    on_demand_tracker.join();
  }
}

void SolverWorldModel::addTypes(std::vector<std::pair<std::u16string, bool>>& new_types) {
  //Store the alias types that this solver will use
	std::vector<world_model::solver::AliasType> new_aliases;
  for (auto I = new_types.begin(); I != new_types.end(); ++I) {
    world_model::solver::AliasType at{(uint32_t)(this->types.size()+1), I->first, I->second};
    this->types.push_back(at);
    aliases[at.type] = at.alias;
    if (I->second) {
      if (on_demand_on.end() == on_demand_on.find(at.alias)) {
        on_demand_on[at.alias] = std::multiset<OnDemandArgs>();
      }
    }
		new_aliases.push_back(at);
  }
  //Update the world model with a new type announcement message
  try {
    std::unique_lock<std::mutex> lck(send_mutex);
    sendAndReconnect(world_model::solver::makeTypeAnnounceMsg(new_aliases, origin));
  }
  catch (std::runtime_error err) {
    std::cerr<<"Problem sending type announce message: "<<err.what()<<'\n';
  }
}

bool SolverWorldModel::connected() {
  if (s) {
    return true;
  }
  else {
    return false;
  }
}

void SolverWorldModel::sendData(std::vector<AttrUpdate>& solution, bool create_uris) {
  using world_model::solver::SolutionData;
  std::vector<SolutionData> sds;
  for (auto I = solution.begin(); I != solution.end(); ++I) {
    std::unique_lock<std::mutex> lck(trans_mutex);
    if (aliases.end() != aliases.find(I->type)) {
      uint32_t alias = aliases[I->type];
      //TODO Let the user check this themselves, it should be up to them
      //whether or not to send data
      //Send if this is not an on_demand or it is an on_demand but is requested
      if (on_demand_on.end() == on_demand_on.find(alias)) {
        SolutionData sd{alias, I->time, I->target, I->data};
        sds.push_back(sd);
      }
      else {
        //Find if any patterns match this information
        if (std::any_of(on_demand_on[alias].begin(), on_demand_on[alias].end(),
              [&](const OnDemandArgs& ta) {
              if (not ta.valid) { return false;}
              regmatch_t pmatch;
              int match = regexec(&ta.exp, toString(I->target).c_str(), 1, &pmatch, 0);
              return (0 == match and 0 == pmatch.rm_so and I->target.size() == pmatch.rm_eo); })) {
          SolutionData sd{alias, I->time, I->target, I->data};
          sds.push_back(sd);
        }
      }
    }
  }

  //Allow sending an empty message (if all of the solutions are unrequested
  //on_demand solutions) to serve as a keep alive.
  std::unique_lock<std::mutex> lck(send_mutex);
  sendAndReconnect(world_model::solver::makeSolutionMsg(create_uris, sds));
}

void SolverWorldModel::createURI(world_model::URI uri, world_model::grail_time created) {
  std::unique_lock<std::mutex> lck(send_mutex);
  sendAndReconnect(world_model::solver::makeCreateURI(uri, created, origin));
}

void SolverWorldModel::expireURI(world_model::URI uri, world_model::grail_time expires) {
  std::unique_lock<std::mutex> lck(send_mutex);
  sendAndReconnect(world_model::solver::makeExpireURI(uri, expires, origin));
}

void SolverWorldModel::deleteURI(world_model::URI uri) {
  std::unique_lock<std::mutex> lck(send_mutex);
  sendAndReconnect(world_model::solver::makeDeleteURI(uri, origin));
}

void SolverWorldModel::expireURIAttribute(world_model::URI uri, std::u16string name, world_model::grail_time expires) {
  std::unique_lock<std::mutex> lck(send_mutex);
  sendAndReconnect(world_model::solver::makeExpireAttribute(uri, name, origin, expires));
}

void SolverWorldModel::deleteURIAttribute(world_model::URI uri, std::u16string name) {
  std::unique_lock<std::mutex> lck(send_mutex);
  sendAndReconnect(world_model::solver::makeDeleteAttribute(uri, name, origin));
}


