#include<iostream>
#include<boost/filesystem/operations.hpp>
#include <stdexcept>

namespace bfs=boost::filesystem;

int main()
{
	try
	{
	    bfs::path p("boost_library_test.cpp");
	    if(bfs::exists(p))
	    {
	    	std::cout<< "Boost libraries found!" <<std::endl;
	        return 0;
	    }
	    return -1;
	}catch(std::exception)
	{
		return -2;
	}
}