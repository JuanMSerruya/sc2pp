set (Boost_USE_STATIC_LIBS ON)
FIND_PACKAGE( Boost REQUIRED COMPONENTS unit_test_framework )

ADD_EXECUTABLE( test_parsers test_parsers.cpp )
TARGET_LINK_LIBRARIES( test_parsers SC2pp ${MPQLib} )

ADD_EXECUTABLE( bitshift_iterator bitshift_iterator.cpp )
TARGET_LINK_LIBRARIES( bitshift_iterator SC2pp ${Boost_LIBRARIES} ${MPQLib} )

IF (CMAKE_COMPILER_IS_GNUCXX)
  set(CMAKE_CXX_FLAGS "-std=c++0x")
ENDIF()

