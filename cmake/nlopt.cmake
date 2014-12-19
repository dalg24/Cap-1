IF(NOT DEFINED NLOPT_INSTALL_DIR)
    SET(NLOPT_INSTALL_DIR "${CMAKE_INSTALL_PREFIX}/nlopt")
ENDIF()

EXTERNALPROJECT_ADD(
    NLOPT
    URL                http://ab-initio.mit.edu/nlopt/nlopt-2.4.2.tar.gz
    DOWNLOAD_DIR       ${NLOPT_SOURCE_DIR}
    SOURCE_DIR         ${NLOPT_SOURCE_DIR}
    INSTALL_DIR        ${NLOPT_INSTALL_DIR}
    CONFIGURE_COMMAND  ${NLOPT_SOURCE_DIR}/configure --prefix=${NLOPT_INSTALL_DIR}
    BUILD_IN_SOURCE    0
)

FILE(APPEND "${CMAKE_INSTALL_PREFIX}/TPLs.cmake" "SET(NLOPT_INSTALL_DIR \"${NLOPT_INSTALL_DIR}\")\n")
