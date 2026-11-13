cmake_minimum_required(VERSION 3.16)
project(nexora-thirdperson LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT NEXORA_CLIENT_ROOT)
	if(DEFINED ENV{NEXORA_SDK_DIR})
	set(NEXORA_CLIENT_ROOT "$ENV{NEXORA_SDK_DIR}")
	elseif(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../sdk/include/engine/APIProxy.h")
	set(NEXORA_CLIENT_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../sdk/include")
	else()
	set(NEXORA_CLIENT_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../nexora/cs16-client")
	endif()
endif()

message(STATUS "Using CS SDK from: ${NEXORA_CLIENT_ROOT}")

if(NOT EXISTS "${NEXORA_CLIENT_ROOT}/engine/APIProxy.h")
	message(FATAL_ERROR "CS SDK not found: ${NEXORA_CLIENT_ROOT} (set NEXORA_SDK_DIR or NEXORA_CLIENT_ROOT)")
endif()

if(CMAKE_ANDROID_ARCH_ABI)
	if(CMAKE_ANDROID_ARCH_ABI STREQUAL "arm64-v8a")
		set(NEXORA_ABI_TAG "arm64")
	elseif(CMAKE_ANDROID_ARCH_ABI STREQUAL "armeabi-v7a")
		set(NEXORA_ABI_TAG "arm")
	elseif(CMAKE_ANDROID_ARCH_ABI STREQUAL "x86_64")
		set(NEXORA_ABI_TAG "x86_64")
	elseif(CMAKE_ANDROID_ARCH_ABI STREQUAL "x86")
		set(NEXORA_ABI_TAG "x86")
	else()
		message(FATAL_ERROR "unsupported Android ABI: ${CMAKE_ANDROID_ARCH_ABI}")
	endif()
elseif(CMAKE_SYSTEM_NAME STREQUAL "Android")
	if(CMAKE_SYSTEM_PROCESSOR STREQUAL "aarch64" OR CMAKE_SYSTEM_PROCESSOR STREQUAL "arm64")
		set(NEXORA_ABI_TAG "arm64")
	elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "^arm")
		set(NEXORA_ABI_TAG "arm")
	elseif(CMAKE_SYSTEM_PROCESSOR STREQUAL "x86_64")
		set(NEXORA_ABI_TAG "x86_64")
	elseif(CMAKE_SYSTEM_PROCESSOR MATCHES "i[3-6]86|^x86$")
		set(NEXORA_ABI_TAG "x86")
	else()
		message(FATAL_ERROR "unsupported Android processor: ${CMAKE_SYSTEM_PROCESSOR}")
	endif()
else()
	if(CMAKE_SIZEOF_VOID_P EQUAL 8)
		set(NEXORA_ABI_TAG "amd64")
	else()
		set(NEXORA_ABI_TAG "i386")
	endif()
endif()

add_library(thirdperson_plugin SHARED src/thirdperson_plugin.cpp)

target_include_directories(thirdperson_plugin PRIVATE
	${CMAKE_CURRENT_SOURCE_DIR}/../include
	${NEXORA_CLIENT_ROOT}/cl_dll
	${NEXORA_CLIENT_ROOT}/cl_dll/include
	${NEXORA_CLIENT_ROOT}/engine
	${NEXORA_CLIENT_ROOT}/common
	${NEXORA_CLIENT_ROOT}/public
	${NEXORA_CLIENT_ROOT}/pm_shared
	${NEXORA_CLIENT_ROOT}/game_shared
	${NEXORA_CLIENT_ROOT}/dlls)

set_target_properties(thirdperson_plugin PROPERTIES
	PREFIX "lib"
	OUTPUT_NAME "thirdperson_nexora_${NEXORA_ABI_TAG}"
	CXX_VISIBILITY_PRESET default
	VISIBILITY_INLINES_HIDDEN OFF)
