cmake_minimum_required(VERSION 3.13)
if(POLICY CMP0079)
	cmake_policy(SET CMP0079 NEW)
endif()

add_executable(client-media main.cpp)

target_link_libraries(client-media LibDataChannel::LibDataChannel Threads::Threads nlohmann_json::nlohmann_json)
