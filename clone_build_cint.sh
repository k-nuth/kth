#to use ./clone.sh ~/112bit/nuevo/
#destination folder must exists

declare -a BOOST_CHECK_RESULTS=("Success" "Headers not found" "Runtime error in header test" "Libraries not found" "Runtime error in library test")

function install_boost
{
	echo "--->Downloading and installing boost..."
	#Download tarball
	wget 'https://dl.bintray.com/boostorg/release/1.64.0/source/boost_1_64_0.tar.gz'
	#Unzip
	tar -xvzf boost_1_64_0.tar.gz
	#Build with -fPIC flag
	cd boost_1_64_0
	./bootstrap.sh
	echo "Need admin permission to install Boost"
	sudo ./b2  cxxflags=-fPIC cflags=-fPIC install
	cd ..
	rm -rf boost_1_64_0.tar.gz
	rm -rf boost_1_64_0
}

function install_cmake
{
	echo "--->Cmake not found, installing"
	wget https://cmake.org/files/v3.7/cmake-3.7.0-rc1.tar.gz
	tar -xvzf cmake-3.7.0-rc1.tar.gz
	cd cmake-3.7.0-rc1
	./bootstrap
	make
	sudo make install
	sudo ln -s /usr/local/bin/cmake /usr/bin/cmake
	cd ..
	rm -rf cmake-3.7.0-rc1.tar.gz
	rm -rf cmake-3.7.0-rc1
}

function check_if_boost_installed
{
	#Check if it is possible to build a header-only Boost program
	if ! g++ boost_header_only_test.cpp -o boost_header_only_test >&/dev/null; then
		echo ${BOOST_CHECK_RESULTS[1]}
		return 1
	fi
	#Run the program and see if it works; also, get Boost version
	if ! ./boost_header_only_test; then
		echo ${BOOST_CHECK_RESULTS[2]}
		return 2
	fi
	#Check if it possible to build a program using a Boost library
	if ! g++ boost_library_test.cpp -o boost_library_test -lboost_filesystem -lboost_system >&/dev/null; then
		echo ${BOOST_CHECK_RESULTS[3]}
		return 3
	fi
	#Check if the program runs fine
	if ! ./boost_library_test; then
		echo ${BOOST_CHECK_RESULTS[4]}
		return 4
	fi
	return 0
}

function check_dependencies
{
	if ! check_if_boost_installed; then
		install_boost
		#TODO Find a way to avoid system restart
		echo "Boost install complete. Please restart system and run this script again"
		exit 0
	fi

	if ! git --help >&/dev/null; then
		echo "--->Git not found, installing..."
		sudo apt-get install git
	fi

	if ! cmake --help&>/dev/null; then
		install_cmake
	fi
}


function build_project
{
	cd $1
	mkdir build
	cd build
    	cmake .. -G "Unix Makefiles" -DENABLE_TESTS=OFF -DWITH_EXAMPLES=OFF -DWITH_REMOTE_DATABASE=OFF -DWITH_REMOTE_BLOCKCHAIN=OFF -DWITH_LITECOIN=OFF -DWITH_TESTS=OFF -DENABLE_MODULE_RECOVERY=ON -DWITH_TOOLS=OFF -DCMAKE_BUILD_TYPE=Debug $2 $3 $4 $5 $6 $7 $8
	make -j4
 	cd ..
 	cd ..
}

function build_bitprim
{
	check_dependencies

	#define variables
	address="./"
	master=$address

	git clone 'https://github.com/bitprim/secp256k1' -b c-api
	git clone 'https://github.com/bitprim/bitprim-core' -b c-api
	git clone 'https://github.com/bitprim/bitprim-network' -b c-api
	git clone 'https://github.com/bitprim/bitprim-consensus' -b c-api
	git clone 'https://github.com/bitprim/bitprim-database' -b c-api
	git clone 'https://github.com/bitprim/bitprim-blockchain' -b c-api
	git clone 'https://github.com/bitprim/bitprim-node' -b c-api
	git clone 'https://github.com/bitprim/bitprim-node-cint' 

	Dblockchain=" -Dbitprim-blockchain_DIR=$master/bitprim-blockchain/build/ "
	Dconsensus=" -Dbitprim-consensus_DIR=$master/bitprim-consensus/build/ "
	Dcore=" -Dbitprim-core_DIR=$master/bitprim-core/build/ "
	Dnetwork=" -Dbitprim-network_DIR=$master/bitprim-network/build/ "
	Dsecp=" -Dsecp256k1_DIR=$master/secp256k1/build/ "
	Ddatabase=" -Dbitprim-database_DIR=$master/bitprim-database/build "
	Dnode=" -Dbitprim-node_DIR=$master/bitprim-node/build/ "
	Ddatabase=" -Dbitprim-database_DIR=$master/bitprim-database/build "

	#Node Install
	build_project secp256k1 $Dblockchain $Dconsensus $Dcore $Dnetwork $Dsecp $Ddatabase 
	build_project bitprim-core $Dblockchain $Dconsensus $Dcore $Dnetwork $Dsecp $Ddatabase 
	build_project bitprim-network $Dblockchain $Dconsensus $Dcore $Dnetwork $Dsecp $Ddatabase 
	build_project bitprim-consensus $Dblockchain $Dconsensus $Dcore $Dnetwork $Dsecp $Ddatabase 
	build_project bitprim-database $Dblockchain $Dconsensus $Dcore $Dnetwork $Dsecp $Ddatabase 
	build_project bitprim-blockchain $Dblockchain $Dconsensus $Dcore $Dnetwork $Dsecp $Ddatabase 
	build_project bitprim-node $Dblockchain $Dconsensus $Dcore $Dnetwork $Dsecp $Ddatabase 
	build_project bitprim-node-cint $Dblockchain $Dconsensus $Dcore $Dnetwork $Dsecp $Ddatabase $Dnode
}

build_bitprim

