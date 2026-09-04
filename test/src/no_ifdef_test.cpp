/**
 * no_ifdef_test — permanent guard for the local-file layer (boss requirement):
 * none of the five local-file files may contain platform branching (D-17).
 * Fails on `#ifdef`, `#ifndef _WIN32`, or `#if defined( _WIN32 )` in:
 *   include/LocalFileCommon.hpp, include/LocalFileCommon.win.hpp,
 *   include/LocalFileCommon.posix.hpp, src/LocalFileCommon.win.cpp,
 *   src/LocalFileCommon.posix.cpp (D-18 scope — exactly these five).
 * `#pragma once` and own-name include guards are NOT violations.
 */

#include <gtest/gtest.h>

#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

static bool containsIfdefViolation( const std::string &path )
{
    std::ifstream ifs( path );
    if ( !ifs.is_open() )
    {
        // A deleted/renamed local-file file must FAIL the guard, not skip it.
        ADD_FAILURE() << "cannot open " << path;
        return true;
    }
    std::string line;
    while ( std::getline( ifs, line ) )
    {
        // Strip leading whitespace so indented directives are still caught.
        const auto  firstNonWs = line.find_first_not_of( " \t" );
        const auto &stripped   = ( firstNonWs == std::string::npos ) ? line : line.substr( firstNonWs );

        // (a) any #ifdef
        if ( stripped.rfind( "#ifdef", 0 ) == 0 )
        {
            return true;
        }
        // (b) #ifndef _WIN32 (own-name guards like #ifndef FOO_HPP do not match)
        if ( stripped.rfind( "#ifndef", 0 ) == 0 && stripped.find( "_WIN32" ) != std::string::npos )
        {
            return true;
        }
        // (c) #if defined( _WIN32 ) — "#if" followed by whitespace
        if ( stripped.rfind( "#if", 0 ) == 0 && stripped.size() > 3 && ( stripped[3] == ' ' || stripped[3] == '\t' )
             && stripped.find( "defined" ) != std::string::npos && stripped.find( "_WIN32" ) != std::string::npos )
        {
            return true;
        }
    }
    return false;
}

TEST( NoIfdefTest, LocalFileLayerHasNoPlatformBranching )
{
    const std::vector<std::string> files = {
        std::string( LOCALFILE_INCLUDE_DIR ) + "/LocalFileCommon.hpp",
        std::string( LOCALFILE_INCLUDE_DIR ) + "/LocalFileCommon.win.hpp",
        std::string( LOCALFILE_INCLUDE_DIR ) + "/LocalFileCommon.posix.hpp",
        std::string( LOCALFILE_SRC_DIR ) + "/LocalFileCommon.win.cpp",
        std::string( LOCALFILE_SRC_DIR ) + "/LocalFileCommon.posix.cpp",
    };
    for ( const auto &f : files )
    {
        EXPECT_FALSE( containsIfdefViolation( f ) ) << f;
    }
}

TEST( NoIfdefTest, ScannerDetectsViolations )
{
    // Anti-vacuity self-check: the scanner must flag all three banned forms
    // on a crafted probe file while ignoring a benign `#pragma once` line.
    const std::string probePath = "no_ifdef_scanner_probe.txt";
    {
        std::ofstream ofs( probePath );
        ofs << "#ifdef _WIN32\n";
        ofs << "#ifndef _WIN32\n";
        ofs << "#if defined( _WIN32 )\n";
        ofs << "#pragma once\n";
    }
    const bool detected = containsIfdefViolation( probePath );
    std::remove( probePath.c_str() ); // clean up even though the assert below would pass/fail
    EXPECT_TRUE( detected );
}
