/*
 * Copyright (c) 2011, Peter Thorson. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 *     * Neither the name of the WebSocket++ Project nor the
 *       names of its contributors may be used to endorse or promote products
 *       derived from this software without specific prior written permission.
 * 
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" 
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE 
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE 
 * ARE DISCLAIMED. IN NO EVENT SHALL PETER THORSON BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 * 
 */

#ifndef WEBSOCKET_PROCESSOR_HYBI_LEGACY_HPP
#define WEBSOCKET_PROCESSOR_HYBI_LEGACY_HPP

#include "processor.hpp"

#include "../md5/md5.hpp"
#include "../network_utilities.hpp"

namespace websocketpp {
namespace processor {

namespace hybi_legacy_state {
	enum value {
		INIT = 0,
		READ = 1,
		DONE = 2
	};
}

template <class connection_type>
class hybi_legacy : public processor_base {
public:
	hybi_legacy(connection_type &connection) 
	 : m_connection(connection),
	   m_state(hybi_legacy_state::INIT) 
	{
		reset();
	}
	
	void validate_handshake(const http::parser::request& headers) const {
		
	}
	
	void handshake_response(const http::parser::request& request,http::parser::response& response) {
		char key_final[16];
		
		// key1
		*reinterpret_cast<uint32_t*>(&key_final[0]) = 
		    decode_client_key(request.header("Sec-WebSocket-Key1"));
		
		// key2
		*reinterpret_cast<uint32_t*>(&key_final[4]) = 
		    decode_client_key(request.header("Sec-WebSocket-Key2"));
		
		// key3
		memcpy(&key_final[8],request.header("Sec-WebSocket-Key3").c_str(),8);
		
		// md5
        m_key3 = key_final;
		m_key3 = md5_hash_string(m_key3);
		
		response.add_header("Upgrade","websocket");
		response.add_header("Connection","Upgrade");
		
		// TODO: require headers that need application specific information?
		
		// Echo back client's origin unless our local application set a
		// more restrictive one.
		if (response.header("Sec-WebSocket-Origin") == "") {
			response.add_header("Sec-WebSocket-Origin",request.header("Origin"));
		}
		
		// Echo back the client's request host unless our local application
		// set a different one.
		if (response.header("Sec-WebSocket-Location") == "") {
			// TODO: extract from host header rather than hard code
			uri_ptr uri = get_uri(request);
			response.add_header("Sec-WebSocket-Location",uri->str());
		}
	}
	
	std::string get_origin(const http::parser::request& request) const {
		return request.header("Origin");
	}
	
	uri_ptr get_uri(const http::parser::request& request) const {
		std::string h = request.header("Host");
		
		size_t found = h.find(":");
		if (found == std::string::npos) {
			return uri_ptr(new uri(m_connection.is_secure(),h,request.uri()));
		} else {
			return uri_ptr(new uri(m_connection.is_secure(),
								   h.substr(0,found),
								   h.substr(found+1),
								   request.uri()));
		}
		
		// TODO: check if get_uri is a full uri
	}
	
	void consume(std::istream& s) {
		//unsigned char c;
		while (s.good() && m_state != hybi_legacy_state::DONE) {
			//c = s.get();
			//if (s.good()) {
				process(s);
			//}
		}
	}
	
	bool ready() const {
		return m_state == hybi_legacy_state::DONE;
	}
	
	// legacy hybi has no control messages.
	bool is_control() const {
		return false;
	}
	
	message::data_ptr get_data_message() {
		message::data_ptr p = m_data_message;
		m_data_message.reset();
		return p;
	}
	
	message::control_ptr get_control_message() {
		throw "Hybi legacy has no control messages.";
	}
	
	void process(std::istream& input) {
		if (m_state == hybi_legacy_state::INIT) {
			// we are looking for a 0x00
			if (input.peek() == 0x00) {
				// start a message
				input.ignore();
				
				m_state = hybi_legacy_state::READ;
				
				m_data_message = m_connection.get_data_message();
				
				if (!m_data_message) {
					throw processor::exception("Out of data messages",processor::error::OUT_OF_MESSAGES);
				}
				
				m_data_message->reset(frame::opcode::TEXT);
			} else {
				input.ignore();
				// TODO: ignore or error
				//std::stringstream foo;
				
				//foo << "invalid character read: |" << input.peek() << "|";
				
				std::cout << "invalid character read: |" << input.peek() << "|" << std::endl;
				
				//throw processor::exception(foo.str(),processor::error::PROTOCOL_VIOLATION);
			}
		} else if (m_state == hybi_legacy_state::READ) {
			if (input.peek() == 0xFF) {
				// end
				input.ignore();
				
				m_state = hybi_legacy_state::DONE;
			} else {
				if (m_data_message) {
					m_data_message->process_payload(input,1);
				}
			}
		}
	}
	
	void reset() {
		m_state = hybi_legacy_state::INIT;
		if (m_data_message) {
			m_connection.recycle(m_data_message);
		}
		m_data_message.reset();
	}
	
	uint64_t get_bytes_needed() const {
		return 1;
	}
	
	std::string get_key3() const {
		return m_key3;
	}
	
	// TODO: to factor away
	binary_string_ptr prepare_frame(frame::opcode::value opcode,
									bool mask,
									const binary_string& payload)  {
		if (opcode != frame::opcode::TEXT) {
			// TODO: hybi_legacy doesn't allow non-text frames.
			throw;
		}
		
		// TODO: mask = ignore?
		
		// TODO: utf8 validation on payload.
		
		binary_string_ptr response(new binary_string(payload.size()+2));
		
		(*response)[0] = 0x00;
		std::copy(payload.begin(),payload.end(),response->begin()+1);
		(*response)[response->size()-1] = 0xFF;
		
		return response;
	}
	
	binary_string_ptr prepare_frame(frame::opcode::value opcode,
									bool mask,
									const utf8_string& payload)  {
		if (opcode != frame::opcode::TEXT) {
			// TODO: hybi_legacy doesn't allow non-text frames.
			throw;
		}
		
		// TODO: mask = ignore?
		
		// TODO: utf8 validation on payload.
		
		binary_string_ptr response(new binary_string(payload.size()+2));
		
		(*response)[0] = 0x00;
		std::copy(payload.begin(),payload.end(),response->begin()+1);
		(*response)[response->size()-1] = 0xFF;
		
		return response;
	}
	
	binary_string_ptr prepare_close_frame(close::status::value code,
										  bool mask,
										  const std::string& reason) {
		binary_string_ptr response(new binary_string(2));
		
		(*response)[0] = 0xFF;
		(*response)[1] = 0x00;
		
		return response;
	}
	
    void prepare_frame(message::data_ptr msg, bool masked, int32_t mask) {
        if (msg->get_prepared()) {
            return;
        }
        
        msg->set_header(std::string(0x00));
        // TODO: append 0xFF
        msg->set_prepared(true);
    }
    
private:
	uint32_t decode_client_key(const std::string& key) {
		int spaces = 0;
		std::string digits = "";
		uint32_t num;
		
		// key2
		for (size_t i = 0; i < key.size(); i++) {
			if (key[i] == ' ') {
				spaces++;
			} else if (key[i] >= '0' && key[i] <= '9') {
				digits += key[i];
			}
		}
		
		num = atoi(digits.c_str());
		if (spaces > 0 && num > 0) {
			return htonl(num/spaces);
		} else {
			return 0;
		}
	}
	
	connection_type&			m_connection;
	hybi_legacy_state::value	m_state;
	
	message::data_ptr			m_data_message;
	
	std::string					m_key3;
};
	
} // processor
} // websocketpp
#endif // WEBSOCKET_PROCESSOR_HYBI_LEGACY_HPP
