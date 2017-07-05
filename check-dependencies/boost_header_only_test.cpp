#include<iostream>
#include<boost/any.hpp>
#include <boost/version.hpp>
#include <stdexcept>

int main()
{
	try
	{
    	boost::any a(5);
    	a = 7.67;
    	double d = boost::any_cast<double>(a);
    	std::cout << "Boost headers found! Boost version: " << BOOST_LIB_VERSION << std::endl;
    	return 0;
	}catch(std::exception e){
		return -1;
	}
}