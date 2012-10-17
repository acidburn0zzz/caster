cmake_minimum_required(VERSION 2.6)

project ( caster )

add_definitions( -O2 -pthread )

set( TFTP /var/lib/tftp )
set( EXECUTABLE_OUTPUT_PATH ${CMAKE_BINARY_DIR}/bin )
set( CMAKE_CACHEFILE_DIR ${CMAKE_BINARY_DIR}/.cache )
set( CMAKE_BUILD_TYPE Custom )

add_library( AsyncLib STATIC
  AsyncLib/Common.cpp
  AsyncLib/Fd.cpp
  AsyncLib/Log.cpp
  AsyncLib/Rate.cpp
  AsyncLib/Sock.cpp
  AsyncLib/Thread.cpp
  AsyncLib/Timer.cpp
  )
  
add_library( HashLib STATIC
  HashLib/Hash.cpp  
  HashLib/md5.c
  )
add_dependencies( HashLib AsyncLib )

add_library( CompressLib STATIC
  CompressLib/Compress.cpp
  CompressLib/fastlz.c
  )
add_dependencies( CompressLib AsyncLib )

include_directories( sqlite3x )
add_definitions(-DSQLITE_OMIT_LOAD_EXTENSION)
add_library( sqlite3x STATIC
  sqlite3x/sqlite3.c
  sqlite3x/sqlite3x_command.cpp
  sqlite3x/sqlite3x_connection.cpp
  sqlite3x/sqlite3x_exception.cpp
  sqlite3x/sqlite3x_reader.cpp
  sqlite3x/sqlite3x_transaction.cpp
  )

add_library( ImageLib STATIC
  ImageLib/Image.cpp
  ImageLib/Device.cpp
  ImageLib/Block.cpp
  )
add_dependencies( ImageLib HashLib CompressLib AsyncLib sqlite3x )

add_library( UdpCastLib STATIC
  UdpCast/UdpCastClient.cpp
  UdpCast/UdpCastServer.cpp
  )
add_dependencies( UdpCastLib AsyncLib )

add_library( CasterLib STATIC
  CasterLib/Client.cpp
  CasterLib/Common.cpp
  CasterLib/Server.cpp
  CasterLib/SessionSender.cpp
  CasterLib/ClientWorker.cpp
  CasterLib/Sender.cpp
  CasterLib/Session.cpp
  CasterLib/SessionClient.cpp
  )
add_dependencies( CasterLib UdpCastLib ImageLib HashLib CompressLib AsyncLib )

add_executable( caster
  Caster.cpp
  )
add_dependencies( caster CasterLib ImageLib HashLib UdpCastLib CompressLib AsyncLib )
target_link_libraries( caster CasterLib ImageLib HashLib UdpCastLib CompressLib AsyncLib sqlite3x pthread z )
set_target_properties( caster PROPERTIES LINK_FLAGS "-static" )

add_executable( udpCast
  UdpCast/UdpCast.cpp
  )
target_link_libraries( udpCast UdpCastLib AsyncLib pthread )
set_target_properties( udpCast PROPERTIES LINK_FLAGS "-static" )

add_executable( casterD
  Caster.cpp
  )
add_dependencies( casterD CasterLib ImageLib HashLib UdpCastLib CompressLib AsyncLib )
target_link_libraries( casterD CasterLib ImageLib HashLib UdpCastLib CompressLib AsyncLib sqlite3x pthread z )

add_custom_target( caster-image ALL
  ${CMAKE_BINARY_DIR}/bin/make_image ${EXECUTABLE_OUTPUT_PATH} ${EXECUTABLE_OUTPUT_PATH}/caster.img
  DEPENDS caster udpCast
  )
  
install(
    TARGETS caster
    RUNTIME DESTINATION /usr/bin
)

install(
    TARGETS casterD
    RUNTIME DESTINATION /usr/bin
)

install(
    TARGETS udpCast
    RUNTIME DESTINATION /usr/local/bin
)

install(
    PROGRAMS ${EXECUTABLE_OUTPUT_PATH}/init.d
    DESTINATION /etc/init.d
    RENAME caster
)

install(
    PROGRAMS ${EXECUTABLE_OUTPUT_PATH}/caster_pxelinux
    DESTINATION /etc/default
)

install(
    PROGRAMS ${EXECUTABLE_OUTPUT_PATH}/default
    DESTINATION /etc/default
	RENAME caster
)

install(
    FILES ${EXECUTABLE_OUTPUT_PATH}/caster.cfg
    DESTINATION /etc
    RENAME caster.cfg-sample
)

install(
    FILES ${EXECUTABLE_OUTPUT_PATH}/caster.img ${EXECUTABLE_OUTPUT_PATH}/vmlinuz ${EXECUTABLE_OUTPUT_PATH}/menu.c32 ${EXECUTABLE_OUTPUT_PATH}/pxelinux.0
    DESTINATION ${TFTP}
)
