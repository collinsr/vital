/*ckwg +29
 * Copyright 2013-2015 by Kitware, Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 *  * Redistributions of source code must retain the above copyright notice,
 *    this list of conditions and the following disclaimer.
 *
 *  * Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 *  * Neither name of Kitware, Inc. nor the names of any contributors may be used
 *    to endorse or promote products derived from this software without specific
 *    prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS ``AS IS''
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHORS OR CONTRIBUTORS BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/**
 * \file
 * \brief config_block IO operations implementation
 */

#include "config_block_io.h"
#include "config_block_exception.h"
#include "config_parser.h"

#include <algorithm>
#include <fstream>
#include <iostream>
#include <sstream>
#include <list>

#include <kwiversys/SystemTools.hxx>
#include <vital/util/tokenize.h>
#include <vital/vital_foreach.h>

namespace kwiver {
namespace vital {

namespace {

// ------------------------------------------------------------------
// Helper method to write out a comment to a configuration file ostream
/**
 * Makes sure there is no trailing white-space printed to file.
 */
void
write_cb_comment( std::ostream& ofile, config_block_description_t const& comment )
{
  typedef config_block_description_t cbd_t;
  size_t line_width = 80;
  cbd_t comment_token = cbd_t( "#" );

  // Add a leading new-line to separate comment block from previous config
  // entry.
  ofile << "\n";

  // preserve manually specified new-lines in the comment string, adding a
  // trailing new-line
  std::list< cbd_t > blocks;
  tokenize( comment, blocks, "\n" );
  while ( blocks.size() > 0 )
  {
    cbd_t cur_block = blocks.front();
    blocks.pop_front();

    // Comment lines always start with the comment token
    cbd_t line_buffer = comment_token;

    // Counter of additional spaces to place in front of the next non-empty
    // word added to the line buffer. There is always at least one space
    // between words.
    size_t spaces = 1;

    std::list< cbd_t > words;
    // Not using token-compress in case there is purposeful use of multiple
    // adjacent spaces, like in bullited lists. This, however, leaves open
    // the appearance of empty-string words in the loop, which are handled.
    tokenize( cur_block, words );
    while ( words.size() > 0 )
    {
      cbd_t cur_word = words.front();
      words.pop_front();

      // word is an empty string, meaning an intentional space was encountered.
      if ( cur_word.size() == 0 )
      {
        ++spaces;
      }
      else
      {
        if ( ( line_buffer.size() + spaces + cur_word.size() ) > line_width )
        {
          ofile << line_buffer << "\n";
          line_buffer = comment_token;
          // On a line split, it makes sense to me that leading spaces are
          // treated as trailing white-space, which should not be output.
          spaces = 1;
        }
        line_buffer += std::string( spaces, ' ' ) + cur_word;
        spaces = 1;
      }
    }

    // flush remaining contents of line buffer if there is anything
    if ( line_buffer.size() > 0 )
    {
      ofile << line_buffer << "\n";
    }
  }
} // write_cb_comment

} //end anonymous namespace


// ------------------------------------------------------------------
config_block_sptr
read_config_file( config_path_t const&      file_path,
                  config_block_key_t const& block_name )
{
  // Check that file exists
  if ( ! kwiversys::SystemTools::FileExists( file_path ) )
  {
    throw config_file_not_found_exception( file_path, "File does not exist." );
  }
  else if ( kwiversys::SystemTools::FileIsDirectory( file_path ) )
  {
    throw config_file_not_found_exception( file_path,
              "Path given doesn't point to a regular file!" );
  }

  kwiver::vital::config_parser the_parser( file_path );
  the_parser.parse_config();
  return the_parser.get_config();
}


// ------------------------------------------------------------------
// Output to file the given \c config_block object to the specified file path
void
write_config_file( config_block_sptr const& config,
                   config_path_t const&     file_path )
{
  using std::cerr;
  using std::endl;

  // If the given path is a directory, we obviously can't write to it.
  if ( kwiversys::SystemTools::FileIsDirectory( file_path ) )
  {
    throw config_file_write_exception( file_path,
          "Path given is a directory, to which we clearly can't write." );
  }

  // Check that the directory of the given filepath exists, creating necessary
  // directories where needed.
  config_path_t parent_dir = kwiversys::SystemTools::GetFilenamePath(
    kwiversys::SystemTools::CollapseFullPath( file_path ) );
  if ( ! kwiversys::SystemTools::FileIsDirectory( parent_dir ) )
  {
    //std::cerr << "at least one containing directory not found, creating them..." << std::endl;
    if ( ! kwiversys::SystemTools::MakeDirectory( parent_dir ) )
    {
      throw config_file_write_exception( parent_dir,
            "Attempted directory creation, but no directory created! No idea what happened here..." );
    }
  }

  // open output file and write each key/value to a line.
  std::ofstream ofile( file_path.c_str() );

  write_config( config, ofile );
  ofile.close();
}


// ------------------------------------------------------------------
void write_config( config_block_sptr const& config,
                   std::ostream&            ofile )
{
  // If there are no config parameters in the given config_block, throw
  if ( ! config->available_values().size() )
  {
    throw config_file_write_exception( "<stream>",
          "No parameters in the given config_block!" );
  }

  // Gather available keys and sort them alphanumerically for a sensibly layout
  // file.
  config_block_keys_t avail_keys = config->available_values();
  std::sort( avail_keys.begin(), avail_keys.end() );


  bool prev_had_descr = false;  // for additional spacing
  VITAL_FOREACH( config_block_key_t key, avail_keys )
  {
    // Each key may or may not have an associated description string. If there
    // is one, write that out as a comment.
    // - comments will be limited to 80 character width lines, including "# "
    //   prefix.
    // - value output format: ``key_path = value\n``

    config_block_description_t descr = config->get_description( key );

    if ( descr != config_block_description_t() )
    {
      //std::cerr << "[write_config_file] Writing comment for '" << key << "'." << std::endl;
      write_cb_comment( ofile, descr );
      prev_had_descr = true;
    }
    else if ( prev_had_descr )
    {
      // Add a spacer line after a k/v with a description
      ofile << "\n";
      prev_had_descr = false;
    }

    ofile << key << " = " << config->get_value< config_block_value_t > ( key ) << "\n";
  }
  ofile.flush();
} // write_config_file

} }   // end namespace
