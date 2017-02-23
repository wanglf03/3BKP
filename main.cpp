/**
 * @author Mirko Bez
 * @file main.cpp
 * 
 * Usage: ./main <filename.dat>
 */
#include <iostream>
#include <fstream>
#include <vector>
#include <time.h>
#include <sys/time.h>
#include <cmath>
#include "cpxmacro.h"
#include <getopt.h>
#include <unistd.h>
#include "Instance3BKP.h"


using namespace std;

// error status and messagge buffer
int status;
char errmsg[BUF_SIZE];		
const int NAME_SIZE = 512;
char name[NAME_SIZE];
bool output_required = true;
bool print = false;
bool benchmark = false;

/** Struct containing the long options */
static struct option long_options[] = {	
	{"help", no_argument, 0, 'h'},
	{"print", no_argument, 0, 'p'},
	{"output", no_argument, 0, 'o'},
	{"benchmark-output", no_argument, 0, 'b'},
	{0, 0, 0, 0},
};






/**
 * set up the model for CPLEX
 * @param env the cplex enviroment
 * @param lp the cplex problem
 * @param N the number of nodes of the TSP
 * @param C the matrix containing the costs from i to j
 * @param mapY used in order to have the result outside the setup function.
 */	
void setupLP(CEnv env, Prob lp, Instance3BKP instance)
{	
	
	int current_var_position 	= 0;
	int N = instance.N;
	int R = 6;
	vector< int > mapT(N);
	vector< vector < vector < int > > > mapB(N, vector<vector<int>>(N, vector<int>(3)));
	vector< vector < int > > mapRho(N, vector< int >(R));
	vector< vector < int > > mapChi(N, vector < int >(3));
	
	
	/********************************************************
	 * 
	 * Setting up the variables
	 * 
	 ********************************************************/
	/* 
	 * adding t variables
	 * t_i := binary value that is one if item is loaded in the KP 
	 */ 
	for(int i = 0; i < N; i++){
			char xtype = 'B';
			double obj = instance.profit[i];
			double lb = 0.0;
			double ub = 1.0;
			sprintf(name, "t_%d", i);
			char* xname = (char*)(&name[0]);
			CHECKED_CPX_CALL( CPXnewcols, env, lp, 1, &obj, &lb, &ub, &xtype, &xname );
			mapT[i] = current_var_position++;
	}
	/* 
	 * adding b_ij^\delta variables
	 * b_{ij}^\delta 1 if item i comes before item j in dimension \delta.
	 * 
	 */ 
	for(int i = 0; i < N; i++){
		for(int j = 0; j < N; j++){
			for(int delta = 0; delta < 3; delta++){
				char xtype = 'B';
				double obj = 0;
				double lb = 0.0;
				double ub = 1.0;
				snprintf(name, NAME_SIZE, "b(%d,%d,%d)", i, j, delta);
				char* xname = (char*)(&name[0]);
				CHECKED_CPX_CALL( CPXnewcols, env, lp, 1, &obj, &lb, &ub, &xtype, &xname );
				mapB[i][j][delta] = current_var_position++;
			}
		}
	}
	
	/* 
	 * adding b_ij^\delta variables
	 * b_{ij}^\delta 1 if item i comes before item j in dimension \delta.
	 * 
	 */ 
	for(int i = 0; i < N; i++){
		for(int r = 0; r < 6; r++){
			char xtype = 'B';
			double obj = 0.0;
			double lb = 0.0;
			double ub = 1.0;
			snprintf(name, NAME_SIZE, "rho_%d_%d", i, r);
			char* xname = (char*)(&name[0]);
			CHECKED_CPX_CALL( CPXnewcols, env, lp, 1, &obj, &lb, &ub, &xtype, &xname );
			mapRho[i][r] = current_var_position++;
		}
	}
	/*
	 * 
	 * adding \chi_i^\delta variables
	 * 
	 */
	 
	 for(int i = 0; i < N; i++){
		for(int delta = 0; delta < 3; delta++){
			char xtype = 'C';
			double obj = 0.0;
			double lb = 0.0;
			double ub = CPX_INFBOUND;
			snprintf(name, NAME_SIZE, "CHI_%d^%d", i, delta);
			char * xname = (char*)(&name[0]);
			CHECKED_CPX_CALL( CPXnewcols, env, lp, 1, &obj, &lb, &ub, &xtype, &xname );
			mapChi[i][delta] = current_var_position++;
		}
	 }

	 cout << "Number of variables: " << current_var_position << endl;
	/**************************************************
	 * 
	 * Setting up the constraints
	 * 
	 **************************************************/
	
	//Constraint (6): sum_{j \in J} w_j*d_j*h_j*t_j <= WDH
	//Creating additional scope in order to avoid future name conflicts (I will use idx other times)
	{
		vector<double> coef(N);
		//starting from without considering j
		for(int j = 0; j < N; j++){
			coef[j] = instance.getVolume(j);
		}
		char sense = 'L';
		int matbeg = 0;
		double rhs = instance.getBoxVolume();
		snprintf(name, NAME_SIZE, "(6)");
		char* cname = (char*)(&name[0]);
		CHECKED_CPX_CALL( CPXaddrows, env, lp, 0, 1, N, &rhs, &sense, &matbeg, &mapT[0], &coef[0], 0, &cname);
	}
	
	//Constraint (7) : (\sum_{\delta \in \Delta} b_ij^\delta + b_ij^\delta) -t_i -t_j >= -1
	// quindi -(\sum_{\delta \in \Delta} b_ij^\delta + b_ij^\delta) +t_i +t_j <= 1
	for(int i = 0; i < N; i++){
		for(int j = 0; j < N; j++){//Add i < j
			if(i >= j)
				continue;
			//|\Delta|*2 + 2
			vector < int > idx(8);
			vector < double > coeff(8);
			int index = 0;
			for(int delta = 0; delta < 3; delta++){
				idx[index] = mapB[i][j][delta];
				idx[index+1] = mapB[j][i][delta];
				coeff[index] = -1.0;
				coeff[index+1] = -1.0;
				index += 2;
			}
			idx[index] = mapT[i];
			idx[index+1] = mapT[j];
			coeff[index] = 1.0;
			coeff[index+1] = 1.0;
			
			char sense = 'L';
			int matbeg = 0;
			double rhs = 1.0;
			snprintf(name, NAME_SIZE, "(7) %d %d", i, j);
			char* cname = (char*)(&name[0]);
			
			CHECKED_CPX_CALL( CPXaddrows, env, lp, 0, 1, idx.size(), &rhs, &sense, &matbeg, &idx[0], &coeff[0], 0, &cname);

		}
	}
		
	/* (8) */
	for(int i = 0; i < N; i++){
		for(int delta = 0; delta < 3; delta++){
			vector< int > idx(7);
			vector< double > coeff(7);
			int index = 0;
			idx[index] = mapChi[i][delta];
			coeff[index] = 1.0;
			for(int r = 0; r < R; r++){
				idx[index] = mapRho[i][r];
				coeff[index] = instance.s[i][instance.R[r][delta]];
				index++;
			}
			char sense = 'L';
			int matbeg = 0;
			double rhs = instance.S[delta];
			snprintf(name, NAME_SIZE, "(8) %d %d",i,delta);
			char* cname = (char*)(&name[0]);
			CHECKED_CPX_CALL( CPXaddrows, env, lp, 0, 1, idx.size(), &rhs, &sense, &matbeg, &idx[0], &coeff[0], 0, &cname);
		}
	}
	
	/* (9) */
	for(int i = 0; i < N; i++){
		for(int j = 0; j < N; j++){
			if(i >= j)
				continue;
			for(int delta = 0; delta < 3; delta++){
				vector< int > idx(9);
				vector< double > coeff(9);
				
				int index = 0;
				idx[index] = mapChi[i][delta];
				coeff[index] = 1.0;
				for(int r = 0; r < R; r++){
					idx[index] = mapRho[i][r];
					coeff[index] = instance.s[i][instance.R[r][delta]];
					index++;
				}
				idx[index] = mapChi[j][delta];
				coeff[index] = -1.0;
				index++;
				idx[index] = mapB[i][j][delta];
				coeff[index] = instance.M;
				
				char sense = 'L';
				int matbeg = 0;
				double rhs = instance.S[delta];
				snprintf(name, NAME_SIZE, "(9) %d %d",i,delta);
				char* cname = (char*)(&name[0]);
				CHECKED_CPX_CALL( CPXaddrows, env, lp, 0, 1, idx.size(), &rhs, &sense, &matbeg, &idx[0], &coeff[0], 0, &cname);
			}
		}
	}
	
	
	/* (10) */
	for(int i = 0; i < N; i++){
		for(int j = 0; j < N; j++){
			if(i >= j)
				continue;
			for(int delta = 0; delta < 3; delta++){
				vector< int > idx(9);
				vector< double > coeff(9);
				
				int index = 0;
				idx[index] = mapChi[j][delta];
				coeff[index] = 1.0;
				for(int r = 0; r < R; r++){
					idx[index] = mapRho[i][r];
					coeff[index] = instance.s[i][instance.R[r][delta]];
					index++;
				}
				idx[index] = mapChi[i][delta];
				coeff[index] = -1.0;
				index++;
				idx[index] = mapB[i][j][delta];
				coeff[index] = instance.M;
				
				char sense = 'L';
				int matbeg = 0;
				double rhs = instance.S[delta];
				snprintf(name, NAME_SIZE, "(10) %d %d",i,delta);
				char* cname = (char*)(&name[0]);
				CHECKED_CPX_CALL( CPXaddrows, env, lp, 0, 1, idx.size(), &rhs, &sense, &matbeg, &idx[0], &coeff[0], 0, &cname);
			}
		}
	}
		
		
	//Constraint (11) 
	for(int i = 0; i < N; i++){
		for(int delta = 0; delta < 3; delta++){
			vector< int > idx(2);
			vector< double > coeff(2);
			idx[0] = mapChi[i][delta];
			coeff[0] = 1.0;
			idx[1] = mapT[i];
			coeff[1] = -instance.M;
			char sense = 'L';
				int matbeg = 0;
				double rhs = instance.S[delta];
				snprintf(name, NAME_SIZE, "(10) %d %d",i,delta);
				char* cname = (char*)(&name[0]);
				CHECKED_CPX_CALL( CPXaddrows, env, lp, 0, 1, idx.size(), &rhs, &sense, &matbeg, &idx[0], &coeff[0], 0, &cname);
		}
	}
	//Constraint (12): b_{ij}^\delta <= t_i ==> b_{ij}^\delta - t_i = 0
	{
		for(int i = 0; i < N; i++){
			for(int j = 0;  j < N; j++){
				for(int delta = 0; delta < 3; delta++){
					vector <int> idVar(2);
					vector <double> coef(2);
					idVar[0] = mapB[i][j][delta];
					idVar[1] = mapT[i];
					coef[0] = 1.0;
					coef[1] = -1.0;
					char sense = 'L';
					int matbeg = 0;
					double rhs = 0;
					snprintf(name, NAME_SIZE, "(12) %d %d %d", i, j, delta);
					char * cname = (char*) (&name[0]);
					CHECKED_CPX_CALL( CPXaddrows, env, lp, 0, 1, idVar.size(), &rhs, &sense, &matbeg, &idVar[0], &coef[0], 0, &cname);
				}
			}
		}
	}
	
	//Constraint (13): b_{ji}^\delta <= t_j ==> b_{ji}^\delta - t_j = 0
	{
		for(int i = 0; i < N; i++){
			for(int j = 0;  j < N; j++){
				for(int delta = 0; delta < 3; delta++){
					vector <int> idVar(2);
					vector <double> coeff(2);
					idVar[0] = mapB[j][i][delta];
					idVar[1] = mapT[j];
					coeff[0] = 1.0;
					coeff[1] = -1.0;
					char sense = 'L';
					int matbeg = 0;
					double rhs = 0;
					snprintf(name, NAME_SIZE, "(13) %d %d %d", i,j,delta);
					char * cname = (char*) (&name[0]);
					CHECKED_CPX_CALL( CPXaddrows, env, lp, 0, 1, idVar.size(), &rhs, &sense, &matbeg, &idVar[0], &coeff[0], 0, &cname);
				}
			}
		}
	}
	
	
	/* Constraint (16) */
	for(int i = 0; i < N; i++){
		vector < int > idx(R);
		vector < double > coeff(R);
		for(int r = 0; r < R; r++){
			idx[r] = mapRho[i][r];
			coeff[r] = 1.0;
		}
		double rhs = 1.0;
		char sense = 'L';
		int matbeg = 0;
		
		snprintf(name, NAME_SIZE, "(16) %d", i);
		char * cname = (char*) (&name[0]);
		CHECKED_CPX_CALL( CPXaddrows, env, lp, 0, 1, idx.size(), &rhs, &sense, &matbeg, &idx[0], &coeff[0], 0, &cname);

	}
	if(output_required){
		cout << "OKI" << endl;
		CHECKED_CPX_CALL( CPXwriteprob, env, lp, "/tmp/Model.lp", NULL ); 
	}
	
	
}

/**
 * solve the model write the result in a file and returns the return value of the function
 * @param env the CPLEX environment
 * @param lp the CPLEX problem
 * @param N the number of nodes of the TSP
 * @param mapY a map containing the (CPLEX) index of the y variables of the problem
 * used in order to retrieve the values of the y variables and print them on the screen.
 * @return objval the optimal solution
 */
double solve( CEnv env, Prob lp, Instance3BKP instance) {
	int N = instance.N;
	clock_t t1,t2;
    t1 = clock();
    struct timeval  tv1, tv2;
    gettimeofday(&tv1, NULL);
	//TODO change mipopt with corresponding max
	CHECKED_CPX_CALL( CPXmipopt, env, lp );
	t2 = clock();
    gettimeofday(&tv2, NULL);
	double objval = 0.0;
	CHECKED_CPX_CALL( CPXgetobjval, env, lp, &objval );
	
	double user_time = (double)(tv2.tv_sec+tv2.tv_usec*1e-6 - (tv1.tv_sec+tv1.tv_usec*1e-6));
	double cpu_time = (double)(t2-t1) / CLOCKS_PER_SEC;
	
	
	if(output_required){	
		CHECKED_CPX_CALL( CPXsolwrite, env, lp, "Model.sol");
	}
	if(benchmark){
		printf("%4d\t%12.6f\t%12.6f\n", N, cpu_time, objval);//, user_time, objval);
	} else {
		cout << "Problem Size N" << N << endl;
		cout << "in " << user_time << " seconds (user time)\n";
		cout << "in " << cpu_time << " seconds (CPU time)\n";
		cout << "Objval: " << objval << endl;
	}
	return objval;
}



/** 
 * print the help message
 * @param argv in order to get the name of the compiled program
 */
void print_help(char * argv[]){
	cout << "Usage : " << argv[0] << " <instance.dat> " << " [OPT]... " << endl;
	cout << endl;
	cout << "-h, --help\t\t\tprint this message and exit" << endl;
	cout << "-o, --output\t\t\twrite the solved problem in a file name Model.sol" << endl;
	cout << "-p, --print\t\t\tat the end print the list of cities, e.g. 0 1 2 3 4 0" << endl;
	cout << endl;
}


/**
 * function that according to the given options set the flags
 * 	--output, in order to print the solved problem in the given file
 *  --print, in order to print the TSP-Solution 0 <1, 2 .. n> 0
 *  --benchmark_output print a special form of output N user_time cpu_time objval
 * @param argc 
 * @param argv
 */
Instance3BKP get_option(int argc,  char * argv[]){
	int c;
	if(argc < 2) {
		print_help(argv);
		exit(EXIT_FAILURE);
	}
	// One can specify only the help flag without the instance file.
	if(strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0){
		print_help(argv);
	}
	optind = 2; //Starting from index 2, because the first place is destinated to the istance file.
	int option_index;
	while((c = getopt_long (argc, argv, "hopb", long_options, &option_index)) != EOF) {
		switch(c){
			case 'h': print_help(argv); exit(EXIT_SUCCESS); break;
			case 'o': output_required=true; break;
			case 'p': print = true; break;
			case 'b': benchmark = true; break;
			 
		}
    }
    Instance3BKP instance(argv[1]);
	instance.print();
    return instance;
}

/**
 * the main function
 * @param argc
 * @param argv
 * @return EXIT_SUCCESS on success, EXIT_FAILURE otherwise
 */
int main (int argc, char *argv[])
{
	
	
	
	bool exc_arised = false;
	
	try { 
		

		Instance3BKP instance = get_option(argc, argv);	
		
		
		DECL_ENV( env );
		DECL_PROB( env, lp );
		setupLP(env, lp, instance);
		
		// find the solution
		//solve(env, lp, instance);
		// free-allocated resolve
		CPXfreeprob(env, &lp);
		CPXcloseCPLEX(&env);
	} catch (exception& e){
		cout << ">>Exception: " << e.what() << endl;
		exc_arised = true;
	}
	
	return exc_arised?EXIT_FAILURE:EXIT_SUCCESS;
}
