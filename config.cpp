#include "config.h"

Config::Config( const std::string &filename )
{    
    try {
	using boost::property_tree::ptree;
	ptree pt;

	read_ini(filename, pt);
	
	for( auto &sections : pt ) {
	    std::string section_name = sections.first.data();
	    if ( section_name.find( "Time grid" ) != std::string::npos ) {
		time_config_part = Time_config_part( sections.second );
	    } else if ( section_name.find( "Spatial mesh" ) != std::string::npos ) {		
		mesh_config_part = Mesh_config_part( sections.second );
	    } else if ( section_name.find( "Source." ) != std::string::npos ) {
		std::string source_name = section_name.substr( section_name.find(".") + 1 );
		sources_config_part.emplace_back( source_name, sections.second );
	    } else if ( section_name.find( "Boundary conditions" ) != std::string::npos ) {
		boundary_config_part = Boundary_config_part( sections.second );
	    } else if ( section_name.find( "Output filename" ) != std::string::npos ) {
		output_filename_config_part = Output_filename_config_part( sections.second );				
	    } else {
		std::cout << "Ignoring unknown section: " << section_name << std::endl;
	    }
	}
	return;
    }
    catch( std::exception& e ) {
        std::cerr << "error: " << e.what() << "\n";
        exit( EXIT_FAILURE);
    }
    catch( ... ) {
        std::cerr << "Exception of unknown type!\n";
    }
}