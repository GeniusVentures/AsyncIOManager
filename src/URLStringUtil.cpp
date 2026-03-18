//
// Created by Super Genius on 6/12/23.
//

#include "URLStringUtil.h"

extern bool getURLComponents( std::string url, std::string &prefix, std::string &base, std::string &extension )
{
    // Check for empty URL
    if ( url.empty() )
    {
        return false;
    }

    // Find the first occurrence of "://" in the URL.
    size_t index = url.find( "://" );
    // If "://" is not found, then the URL has no prefix.
    if ( index == std::string::npos )
    {
        return false;
    }

    prefix             = url.substr( 0, index );
    base               = url.substr( index + 3, url.length() );
    size_t start_index = base.rfind( "." );
    if ( start_index == std::string::npos )
    {
        extension = "";
    }
    else
    {
        extension = base.substr( start_index + 1, base.length() );
    }

    return true;
}

extern bool parseHTTPUrl( std::string url, std::string &host, std::string &path, std::string &port )
{
    // Check for empty URL
    if ( url.empty() )
    {
        return false;
    }

    // Find the first occurrence of "/" in the URL.
    size_t index = url.find( "/" );
    // If "/" is not found, then the URL has no path.
    if ( index == std::string::npos )
    {
        return false;
    }

    host = url.substr( 0, index );
    path = url.substr( index, url.length() );

    // Check if host is empty after extraction
    if ( host.empty() )
    {
        return false;
    }

    //Look for : in the host to see if a port exists
    size_t port_index = host.find( ":" );
    if ( port_index == std::string::npos )
    {
        //Default to 443
        port = "443";
    }
    else
    {
        port = host.substr( port_index + 1, host.length() );
        host = host.substr( 0, port_index );

        // Check if port is empty or host became empty
        if ( port.empty() || host.empty() )
        {
            return false;
        }
    }

    return true;
}

extern bool parseSFTPUrl( std::string  url,
                          std::string &host,
                          std::string &path,
                          std::string &user,
                          std::string &pass,
                          std::string &publickey_file,
                          std::string &privatekey_file,
                          std::string &privatekey_pass )
{
    // Check for empty URL
    if ( url.empty() )
    {
        return false;
    }

    // Initialize all output parameters
    user            = "";
    pass            = "";
    publickey_file  = "";
    privatekey_file = "";
    privatekey_pass = "";

    std::string working_url = url;

    // Find the first occurrence of "@" in the URL.
    size_t index = working_url.find( "@" );
    // If "@" is not found, then we have no user/pass
    if ( index == std::string::npos )
    {
        user = "";
        pass = "";
        host = working_url;
    }
    else
    {
        user = working_url.substr( 0, index );
        host = working_url.substr( index + 1, working_url.length() );
    }

    //Find Username and Pass
    if ( !user.empty() )
    {
        index = user.find( ":" );
        // If ":" is not found, then we have no password minimally
        if ( index == std::string::npos )
        {
            pass = "";
        }
        else
        {
            pass = user.substr( index + 1, user.length() );
            user = user.substr( 0, index );
        }

        //Find public key location
        index = pass.find( "pubkey_identifier" );
        // If "pubkey_identifier" is not found, then we have no key
        if ( index != std::string::npos )
        {
            publickey_file = pass.substr( index + 17, pass.length() );
            pass           = "";
        }

        //Find Private key location
        index = pass.find( "privkey_identifier" );
        // If "privkey_identifier" is not found, then we have no private key
        if ( index != std::string::npos )
        {
            privatekey_file = pass.substr( index + 18, pass.length() );
            pass            = "";
        }

        //Find Private key passphrase
        if ( !privatekey_file.empty() )
        {
            index = privatekey_file.find( "key_passphrase" );
            // If "key_passphrase" is not found, then we have no passphrase
            if ( index != std::string::npos )
            {
                privatekey_pass = privatekey_file.substr( index + 14, privatekey_file.length() );
                privatekey_file = privatekey_file.substr( 0, index );
            }
        }
    }

    //Find host and path
    index = host.find( "/" );
    // If "/" is not found, then the URL is missing full path
    if ( index == std::string::npos )
    {
        return false;
    }
    else
    {
        path = host.substr( index, host.length() );
        host = host.substr( 0, index );

        // Check if host is empty after extraction
        if ( host.empty() )
        {
            return false;
        }
    }

    return true;
}

extern bool parseIPFSUrl( std::string url, std::string &cid, std::string &file )
{
    // Check for empty URL
    if ( url.empty() )
    {
        return false;
    }

    // Find the first occurrence of "/" in the URL.
    size_t index = url.find( "/" );
    // If "/" is not found, then the URL has no file component.
    if ( index == std::string::npos )
    {
        return false;
    }

    cid  = url.substr( 0, index );
    file = url.substr( index + 1, url.length() );

    // Check if cid is empty after extraction
    if ( cid.empty() )
    {
        return false;
    }

    return true;
}
