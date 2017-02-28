/**
 * @author Mirko Bez
 * @file main.cpp
 * @brief help class containing the 3BKP instance. It is accountable
 * for the parsing of the input file
 */
#include <iostream>
#include <fstream>
#include <vector>
#include <numeric>
#include <stdexcept>

class Instance3BKP {
	private : 
	Instance3BKP() : s(0, std::vector<double>(0)),mass(0) , profit(0) { }
	
	public:
		/** Size of the 3D Box */
		double S[3];
		/** Alias for S[0] */
		double W;
		/** Alias for S[1] */
		double D;
		/** Alias for S[2] */
		double H;
		
		/** Used only if the stability constraint are considered */
		double L[3];
		/** Used only if the stability constraint are considered */
		double U[3];
		
		/** True if centroid are considered, False otherwise */
		bool extended;
		
		/** vector containing the three dimension of the items */
		std::vector< std::vector< double > > s;
		
		/** vector containing the masses of the items */
		std::vector< double > mass;
		
		/** vector containing the profits of the items */
		std::vector< double > profit;
		
		
		
		
		/** Set of rotation of the object. i.e. permutation of 1,2,3 */
		double R [6][3] = {
			{0,1,2},
			{0,2,1},
			{1,0,2},
			{1,2,0},
			{2,0,1},
			{2,1,0},
		};
		
	public:
		/**
		 * @return the volume of the box
		 */
		double getBoxVolume(){
			return W*D*H;
		}
		/**
		 * @return the volume of the i-th item
		 */
		double getVolume(int i){
			return s[i][0]*s[i][1]*s[i][2];
		}
		
		/* Number of items */
		int N;
		/**
		 * Constructor
		 * @param filename name of the file to parse
		 * @param _extended true if the model is extended with 
		 * stability constraint, false otherwise
		 */
		Instance3BKP(const char * filename, bool _extended){
			extended = _extended;
			std::ifstream infile(filename);
			if(!infile.good()) {
				throw std::runtime_error("The given file does not exist");
			}
			infile >> S[0] >> S[1] >> S[2];
			W = S[0];
			D = S[1];
			H = S[2];
			infile >> N;
			s.resize(N);
			for(auto &i : s) i.resize(3);
			
			mass.resize(N);
			profit.resize(N);
			
			for(int i = 0; i < N; i++){
				infile >> s[i][0] >> s[i][1] >> s[i][2] >> mass[i] >> profit[i];
			}

			if(extended){
				infile >> L[0] >> L[1] >> L[2];
				infile >> U[0] >> U[1] >> U[2];			
			}
			
			infile.close();
		}
		
		/**
		 * Print the instance
		 */
		void print(){
			std::cout << S[0] << " " << S[1] << " " << S[2] << std::endl;
			for(int i = 0; i < N; i++){
				std::cout << s[i][0] << " " << s[i][1] << " " << s[i][2] << " " << mass[i] << " " << profit[i] << std::endl;
			}
		}


};

