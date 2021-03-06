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
#include "../Utils/cpxmacro.h"
#include <getopt.h>
#include <unistd.h>
#include "Instance3BKP.h"
#include "../Utils/fetch_option.h"
#include <assert.h>     /* assert */
#include <signal.h>
#include <unistd.h>


bool optimize[3] = {
	true, true, true
};


double timeout;


//int CPXXgetcallbackinfo( CPXCENVptr env, void * cbdata, int wherefrom, int whichinfo, void * result_p ) 

char FILENAME[128];
char OUTPUTFILENAME[128];


using namespace std;

// error status and messagge buffer
int status;
char errmsg[BUF_SIZE];		
const int NAME_SIZE = 512;
char name[NAME_SIZE];


/**
 * Struct that contains all the vectors used to keep track of the variables
 */
struct mapVar {
	private : 
	mapVar() : T(0), B(0, vector<vector<int>>(0, vector<int>(0))) , Rho(0, vector< int >(0)), Chi(0, vector< int >(0)) { }
	public : 
	vector< int > T;
	vector< vector < vector < int > > > B;
	vector< vector < int > > Rho;
	vector< vector < int > > Chi;
	
	mapVar(int N, int R){
		T.resize(N);
		B.resize(N);
		for(auto &i : B) {
			i.resize(N);
			for(auto &j : i) {
				j.resize(3);
			}
		}
		Rho.resize(N);
		for(auto &i : Rho){
			i.resize(R);
		}
		Chi.resize(N);
		for(auto &i : Chi){
			i.resize(3);
		}
	}
};


/**
 * set up the variables
 * @param env
 * @param lp
 * @param instance
 * @param map the map that is filled up with the indexes
 * 
 */
void setupLPVariables(CEnv env, Prob lp, Instance3BKP instance, mapVar &map){
	int current_var_position = 0;
	int N = instance.N;
	/*
	 * 
	 * adding \chi_i^\delta variables
	 * the coordinate of the bottom-left-back point of itme i along dimension \delta
	 * + constraint(17)
	 * 
	 */
	 
	 for(int i = 0; i < N; i++){
		for(int delta = 0; delta < 3; delta++){
			char xtype = 'C';
			double obj = 0.0;
			double lb = 0.0;
			double ub = CPX_INFBOUND;
			snprintf(name, NAME_SIZE, "chi %d %d", i, delta);
			char * xname = (char*)(&name[0]);
			CHECKED_CPX_CALL( CPXnewcols, env, lp, 1, &obj, &lb, &ub, &xtype, &xname );
			map.Chi[i][delta] = current_var_position++;
		}
	 }

	/* 
	 * adding t variables
	 * t_j := binary value {1 if j-th item is loaded in the KP 0 otherwise} 
	 * + constraint(18)
	 */ 
	for(int j = 0; j < N; j++){
			char xtype = 'B';
			double obj = instance.profit[j];
			double lb = 0.0;
			double ub = 1.0;
			sprintf(name, "t_%d", j);
			char* xname = (char*)(&name[0]);
			CHECKED_CPX_CALL( CPXnewcols, env, lp, 1, &obj, &lb, &ub, &xtype, &xname );
			map.T[j] = current_var_position++;
	}
	/* 
	 * adding b_ij^\delta variables
	 * b_{ij}^\delta 1 if item i comes before item j in dimension \delta.
	 * + constraint(19)
	 */ 
	for(int i = 0; i < N; i++){
		for(int j = 0; j < N; j++){
			for(int delta = 0; delta < 3; delta++){
				char xtype = 'B';
				double obj = 0;
				double lb = 0.0;
				double ub = 1.0;
				snprintf(name, NAME_SIZE, "b %d %d %d", i, j, delta);
				char* xname = (char*)(&name[0]);
				CHECKED_CPX_CALL( CPXnewcols, env, lp, 1, &obj, &lb, &ub, &xtype, &xname );
				map.B[i][j][delta] = current_var_position++;
			}
		}
	}
	
	/* 
	 * adding rho_{ir} variables
	 * rho_{ir} {1 if item i is rotated with rotation r, 0 otherwise}.
	 * + constraint 20
	 */ 
	for(int i = 0; i < N; i++){
		for(int r = 0; r < 6; r++){
			char xtype = 'B';
			double obj = 0.0;
			double lb = 0.0;
			double ub = 1.0;
			snprintf(name, NAME_SIZE, "rho %d %d", i, r);
			char* xname = (char*)(&name[0]);
			CHECKED_CPX_CALL( CPXnewcols, env, lp, 1, &obj, &lb, &ub, &xtype, &xname );
			map.Rho[i][r] = current_var_position++;
		}
	}
	
	// cout << "Number of variables: " << current_var_position << endl;
}


/**
 * Set up the constraints 
 * @param env
 * @param lp
 * @param instance
 * @param map
 * 
 */
void setupLPConstraints(CEnv env, Prob lp, Instance3BKP instance, mapVar map){
	int N = instance.N;
	int R = 6; //Cardinality of the set R
	//int DELTA = 3; //Cardinality of the set \Delta
	double M = 1e20; //Used in constraint 9-10 
	
	//Constraint (6): sum_{j \in J} w_j*d_j*h_j*t_j <= WDH
	{
		vector<double> coef(N);
		for(int j = 0; j < N; j++){
			coef[j] = instance.getVolume(j);
		}
		char sense = 'L';
		int matbeg = 0;
		double rhs = instance.getBoxVolume();
		snprintf(name, NAME_SIZE, "(6)");
		char* cname = (char*)(&name[0]);
		CHECKED_CPX_CALL( CPXaddrows, env, lp, 0, 1, N, &rhs, &sense, &matbeg, &map.T[0], &coef[0], 0, &cname);
	}
	
	//Constraint (7) : (\sum_{\delta \in \Delta} b_ij^\delta + b_ij^\delta) -t_i -t_j >= -1
	for(int i = 0; i < N; i++){
		for(int j = 0; j < N; j++){
			if(i >= j)
				continue;
					
			vector < int > idVar(8);
			vector < double > coeff(8);
			unsigned int index = 0;
			for(int delta = 0; delta < 3; delta++){
				idVar[index] = map.B[i][j][delta];
				coeff[index] = +1.0;
				index++;
				idVar[index] = map.B[j][i][delta];
				coeff[index] = +1.0;
				index++;
			}
			idVar[index] = map.T[i];
			coeff[index] = -1.0;
			index++;
			idVar[index] = map.T[j];
			coeff[index] = -1.0;
			index++;
			char sense = 'G';
			int matbeg = 0;
			double rhs = -1.0;
			snprintf(name, NAME_SIZE, "(7) %d %d", i, j);
			char* cname = (char*)(&name[0]);
			
			CHECKED_CPX_CALL( CPXaddrows, env, lp, 0, 1, idVar.size(), &rhs, &sense, &matbeg, &idVar[0], &coeff[0], 0, &cname);
			/* Checking that the vector is correctly initialized */
			assert(index == idVar.size());
		}
	}
		
	/* (8) */
	for(int i = 0; i < N; i++){
		for(int delta = 0; delta < 3; delta++){
			vector< int > idVar(7);
			vector< double > coeff(7);
			unsigned int index = 0;
			idVar[index] = map.Chi[i][delta];
			coeff[index] = 1.0;
			index++;
			for(int r = 0; r < R; r++){
				idVar[index] = map.Rho[i][r];
				coeff[index] = instance.s[i][instance.R[r][delta]];
				index++;
			}
			char sense = 'L';
			int matbeg = 0;
			double rhs = instance.S[delta];
			snprintf(name, NAME_SIZE, "(8) %d %d",i,delta);
			char* cname = (char*)(&name[0]);
			CHECKED_CPX_CALL( CPXaddrows, env, lp, 0, 1, idVar.size(), &rhs, &sense, &matbeg, &idVar[0], &coeff[0], 0, &cname);
			/* Checking that the vector is correctly initialized */
			assert(index == idVar.size());
		}
	}
	
	/* (9) */
	for(int i = 0; i < N; i++){
		for(int j = 0; j < N; j++){
			if(i >= j)
				continue;
			for(int delta = 0; delta < 3; delta++){
				vector< int > idVar(9);
				vector< double > coeff(9);
				unsigned int index = 0;
				idVar[index] = map.Chi[i][delta];
				coeff[index] = 1.0;
				index++;
				for(int r = 0; r < R; r++){
					idVar[index] = map.Rho[i][r];
					coeff[index] = instance.s[i][instance.R[r][delta]];
					index++;
				}
				idVar[index] = map.Chi[j][delta];
				coeff[index] = -1.0;
				index++;
				idVar[index] = map.B[i][j][delta];
				coeff[index] = M;
				index++;
				char sense = 'L';
				int matbeg = 0;
				double rhs = M;
				snprintf(name, NAME_SIZE, "(9) %d %d %d",i, j, delta);
				char* cname = (char*)(&name[0]);
				CHECKED_CPX_CALL( CPXaddrows, env, lp, 0, 1, idVar.size(), &rhs, &sense, &matbeg, &idVar[0], &coeff[0], 0, &cname);
			
				/* Checking that the vector is correctly initialized */
				assert(index == idVar.size());
			}
		}
	}
	
	
	/* (10) */
	for(int i = 0; i < N; i++){
		for(int j = 0; j < N; j++){
			if(i >= j)
				continue;
			for(int delta = 0; delta < 3; delta++){
				vector< int > idVar(9);
				vector< double > coeff(9);
				
				unsigned int index = 0;
				idVar[index] = map.Chi[j][delta];
				coeff[index] = 1.0;
				index++;
				for(int r = 0; r < R; r++){
					/* Possible typo in the paper */
					//idVar[index] = map.Rho[i][r];
					idVar[index] = map.Rho[j][r];
					coeff[index] = instance.s[j][instance.R[r][delta]];
					index++;
				}
				idVar[index] = map.Chi[i][delta];
				coeff[index] = -1.0;
				index++;
				idVar[index] = map.B[j][i][delta];
				coeff[index] = M;
				index++;
				char sense = 'L';
				int matbeg = 0;
				double rhs = M;
				snprintf(name, NAME_SIZE, "(10) %d %d %d",i, j, delta);
				char* cname = (char*)(&name[0]);
				CHECKED_CPX_CALL( CPXaddrows, env, lp, 0, 1, idVar.size(), &rhs, &sense, &matbeg, &idVar[0], &coeff[0], 0, &cname);
			
				/* Checking that the vector is correctly initialized */
				assert(index == idVar.size());
			}
		}
	}
		
		
	//Constraint (11) 
	for(int i = 0; i < N; i++){
		for(int delta = 0; delta < 3; delta++){
			vector< int > idVar(2);
			vector< double > coeff(2);
			idVar[0] = map.Chi[i][delta];
			coeff[0] = 1.0;
			idVar[1] = map.T[i];
			coeff[1] = -M;
			char sense = 'L';
			int matbeg = 0;
			double rhs = 0;
			snprintf(name, NAME_SIZE, "(11) %d %d",i,delta);
			char* cname = (char*)(&name[0]);
			CHECKED_CPX_CALL( CPXaddrows, env, lp, 0, 1, idVar.size(), &rhs, &sense, &matbeg, &idVar[0], &coeff[0], 0, &cname);
		}
	}
	//Constraint (12): b_{ij}^\delta <= t_i ==> b_{ij}^\delta - t_i = 0
	{
		for(int i = 0; i < N; i++){
			for(int j = 0;  j < N; j++){
				for(int delta = 0; delta < 3; delta++){
					vector <int> idVar(2);
					vector <double> coef(2);
					idVar[0] = map.B[i][j][delta];
					coef[0] = 1.0;
					idVar[1] = map.T[i];
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
					idVar[0] = map.B[j][i][delta];
					coeff[0] = 1.0;
					idVar[1] = map.T[j];
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
	
	
	/* Constraint (16) - Modified by us because it leaded to unfeasible models, whenever an object
	 * was bigger than the knapsack */
	for(int i = 0; i < N; i++){
		vector < int > idVar(R+1);
		vector < double > coeff(R+1);
		for(int r = 0; r < R; r++){
			idVar[r] = map.Rho[i][r];
			coeff[r] = 1.0;
		}
		idVar[R] = map.T[i];
		coeff[R] = -1.0;
		double rhs = 0.0;
		char sense = 'E';
		int matbeg = 0;
		
		snprintf(name, NAME_SIZE, "(16) %d", i);
		char * cname = (char*) (&name[0]);
		CHECKED_CPX_CALL( CPXaddrows, env, lp, 0, 1, idVar.size(), &rhs, &sense, &matbeg, &idVar[0], &coeff[0], 0, &cname);

	}
}

/**
 * Set up the balncing constraints (14) e (15) 
 * @param env
 * @param lp
 * @param instance
 * @param map
 * 
 */
void setupLPBalancingConstraints(CEnv env, Prob lp, Instance3BKP instance, mapVar map){
	int N = instance.N;
	//(14)
	for(int delta = 0; delta < 3; delta++){
		vector< int > idVar(N + N*6 + N);
		vector< double > coeff(N + N*6 + N);
		int index = 0;
		for(int i = 0; i < N; i++){
			idVar[index] = map.Chi[i][delta];
			coeff[index] = instance.mass[i];
			index++;
		} 
		for(int i = 0; i < N; i++){
			for(int r = 0; r < 6; r++){
				idVar[index] = map.Rho[i][r];
				coeff[index] = instance.mass[i]*(instance.s[i][instance.R[r][delta]]/2.0);
				index++;
			}
		}
		for(int i = 0; i < N; i++){
			idVar[index] = map.T[i];
			coeff[index] = -instance.L[delta]*instance.mass[i];  
			index++;
		}
		double rhs = 0.0;
		char sense = 'G';
		int matbeg = 0;
		
		snprintf(name, NAME_SIZE, "(14) %d", delta);
		char * cname = (char*) (&name[0]);
		CHECKED_CPX_CALL( CPXaddrows, env, lp, 0, 1, idVar.size(), &rhs, &sense, &matbeg, &idVar[0], &coeff[0], 0, &cname);
	}
	
	// (15)
	for(int delta = 0; delta < 3; delta++){
		vector< int > idVar(N + N*6 + N);
		vector< double > coeff(N + N*6 + N);
		int index = 0;
		for(int i = 0; i < N; i++){
			idVar[index] = map.Chi[i][delta];
			coeff[index] = instance.mass[i];
			index++;
		} 
		for(int i = 0; i < N; i++){
			for(int r = 0; r < 6; r++){
				idVar[index] = map.Rho[i][r];
				coeff[index] = instance.mass[i]*(instance.s[i][instance.R[r][delta]]/2.0);
				index++;
			}
		}
		for(int i = 0; i < N; i++){
			idVar[index] = map.T[i];
			coeff[index] = -instance.U[delta]*instance.mass[i];
			index++;
		}
		snprintf(name, NAME_SIZE, "(15) %d", delta);
		double rhs = 0.0;
		char sense = 'L';
		int matbeg = 0;
		
		char * cname = (char*) (&name[0]);
		CHECKED_CPX_CALL( CPXaddrows, env, lp, 0, 1, idVar.size(), &rhs, &sense, &matbeg, &idVar[0], &coeff[0], 0, &cname);
	}
}


/**
 * set up the model for CPLEX
 * @param env the cplex enviroment
 * @param lp the cplex problem
 * @param N the number of nodes of the TSP
 * @param C the matrix containing the costs from i to j
 * @param mapY used in order to have the result outside the setup function.
 */	
mapVar setupLP(CEnv env, Prob lp, Instance3BKP instance, optionFlag oFlag) {	
	
	int N = instance.N;
	int R = 6;
	mapVar map(N, R);
	
	/* We deal with a maximization problem */
	CHECKED_CPX_CALL( CPXchgobjsen, env, lp, CPX_MAX); 
	/* Set up the variables */
	setupLPVariables(env, lp, instance, map);
	/* Set up the constraints */
	setupLPConstraints(env, lp, instance, map);
	
	/* If the problem is extended */
	if(instance.extended)
		setupLPBalancingConstraints(env, lp, instance, map);
	
	
	if(oFlag.output_required){
		CHECKED_CPX_CALL( CPXwriteprob, env, lp, "Model.lp", NULL ); 
	}
	
	
	return map;
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
double solve( CEnv env, Prob lp, Instance3BKP instance, optionFlag oFlag, bool * timeout_reached) {
	clock_t t1,t2;
	t1 = clock();
	struct timeval  tv1, tv2;
	gettimeofday(&tv1, NULL);
	CHECKED_CPX_CALL( CPXmipopt, env, lp );
	t2 = clock();
	gettimeofday(&tv2, NULL);
	
	int status = CPXgetstat(env, lp);
	switch(status){
		case CPXMIP_OPTIMAL: /* Optimal solution found */
			break;
		case CPXMIP_TIME_LIM_FEAS: /* Time limit exceeded, integer solution exists */
			*timeout_reached = true;
			break;
		case CPXMIP_TIME_LIM_INFEAS: /* Time limit exceeded */
			*timeout_reached = true;
		default: break;
	}
	if(status == CPXMIP_TIME_LIM_FEAS){
		cerr << "TIMEOUT REACHED\n" << endl;
	}
	
	
	double objval = 0.0;
	CHECKED_CPX_CALL( CPXgetobjval, env, lp, &objval );
	
	double user_time = (double)(tv2.tv_sec+tv2.tv_usec*1e-6 - (tv1.tv_sec+tv1.tv_usec*1e-6));
	double cpu_time = (double)(t2-t1) / CLOCKS_PER_SEC;
	
	
	if(oFlag.output_required){	
		CHECKED_CPX_CALL( CPXsolwrite, env, lp, "/tmp/Model.sol");
	}
	if(!oFlag.benchmark){
		printf("User time:\t %.4lf seconds ~ %.6lf minutes\n", user_time, (user_time/60));
		printf("CPU time:\t %.4lf seconds ~ %.6lf minutes \n", cpu_time, (cpu_time/60));
		printf("On average %.4lf (virtual) CPUs were used\n", cpu_time/user_time);
		if(oFlag.threads != 0){
			printf("(%d threads)\n", oFlag.threads);
		}
		cout << "Objval: " << objval << endl;
	} else {
		printf("%.4lf, %.4lf, %lf\n", user_time, cpu_time, objval);
	}	
	return objval;
}




/**
 * Print a human readable output in the file named output.txt
 * @param env
 * @param lp
 * @param instance
 * @param map
 * 
 */
void output(CEnv env, Prob lp, Instance3BKP instance, mapVar map, optionFlag oFlag){
	int N = instance.N;
	int begin = map.T[0];
	int end = map.T[N - 1];
	vector< double > t = vector< double >(N);
	vector< vector < double > > chi(N, vector< double >(3));
	vector< vector < double > > rho(N, vector< double >(6));
	
	CHECKED_CPX_CALL( CPXgetx, env, lp, &t[0], begin, end);
	for(int i = 0; i < N; i++){
		int begin = map.Chi[i][0];
		int end = map.Chi[i][2];
		CHECKED_CPX_CALL( CPXgetx, env, lp, &(chi[i][0]), begin, end);
	}
	for(int i = 0; i < N; i++){
		int begin = map.Rho[i][0];
		int end = map.Rho[i][5];
		CHECKED_CPX_CALL( CPXgetx, env, lp, &(rho[i][0]), begin, end);
	}
	vector< vector< int > > rotation(N, vector< int >(3));
	for(int i = 0; i < N; i++){
		
		for(int r = 0; r < 6; r++){
			if(rho[i][r] >= 0.9) { //Taking into account round errors due to the double representation
				for(int delta = 0; delta < 3; delta++){
					rotation[i][delta] = instance.R[r][delta];
				}
				break;
			}
		}
	}
	
	
	sprintf(OUTPUTFILENAME, "/tmp/output%s", oFlag.extended?"_extended":"");
	ofstream outfile(OUTPUTFILENAME);
	if(!outfile.good()) {
		cerr << OUTPUTFILENAME << endl;
		throw std::runtime_error("Cannot open the file ");
	}
	
	outfile << "#Vengono inclusi solo gli oggetti messi nello zaino" << endl;
	outfile << "#Dimensione Zaino " << instance.S[0] << " " << instance.S[1] <<" "<< instance.S[2] << endl; 
	outfile << "#La prima riga indica le dimensioni dello zaino" << endl;
	outfile << "#Formato: i pos_i_x pos_i_y pos_i_z rot_dim_i_x rot_dim_i_y rot_dim_i_z" << endl;
	if(oFlag.extended)
		outfile << "#Le ultime due righe contengono i valori dei lower bounds ed upper bounds" << endl;
	
	outfile << instance.S[0] << " " << instance.S[1] << " " << instance.S[2] << endl;
	for(int i = 0; i < N; i++){
		if(t[i] < 0.1)
			continue;
		//Print only inserted objects 
		outfile << i << "\t";
		
		for(int delta = 0; delta < 3; delta++){
			outfile << chi[i][delta];
			outfile << " ";
		}
		outfile << "\t";
		for(int delta = 0; delta < 3; delta++){
			outfile << instance.s[i][rotation[i][delta]] << " ";
		}
		
		outfile << endl;
		
	}
	if(!oFlag.benchmark && oFlag.extended){
		outfile << "L " << instance.L[0] << " " << instance.L[1] << " " << instance.L[2] << endl;
		outfile << "U " << instance.U[0] << " " << instance.U[1] << " " << instance.U[2] << endl;
	}
	outfile.close();
	
	double orig_obj = 0;
	for(uint i = 0; i < t.size(); i++){
		if(t[i] > 0.9)
			orig_obj += instance.profit[i];
	}
	if(!oFlag.benchmark)
		cout << "The original objective function value is " << orig_obj << endl;
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
	bool timeout_reached = false;
	try { 

		optionFlag oFlag = get_option(argc, argv);	
		Instance3BKP instance(argv[1], oFlag.extended);
				
		DECL_ENV( env );
		DECL_PROB( env, lp );
		mapVar map = setupLP(env, lp, instance, oFlag);
		
		/* Set up timeout */
		CHECKED_CPX_CALL(CPXsetintparam, env, CPX_PARAM_CLOCKTYPE, 1); //Use CPU as timer
		CHECKED_CPX_CALL(CPXsetdblparam, env, CPX_PARAM_TILIM, oFlag.timeout);
		
		
		CHECKED_CPX_CALL(CPXsetintparam, env,  CPX_PARAM_THREADS, oFlag.threads);
		/* Stop after the given time */ 
		
		// find the solution
		solve(env, lp, instance, oFlag, &timeout_reached);
		
		// print output
		output(env, lp, instance, map, oFlag);
		
		// free-allocated resolve
		CPXfreeprob(env, &lp);
		CPXcloseCPLEX(&env);
	} catch (exception& e){
		cerr << ">>Exception in processing " << argv[1] << ": " << e.what() << endl;
		exc_arised = true;
	}
	if(exc_arised){
		return EXIT_FAILURE;
	} 
	if(timeout_reached){
		return 124;
	}
	return EXIT_SUCCESS;
}
