/**
 * @author Mirko Bez
 * @file main.cpp
 * 
 * 
 * Usage: ./main <filename.dat>@
 */
#include <iostream>
#include <fstream>
#include <vector>
#include <time.h>
#include <sys/time.h>
#include "../Utils/cpxmacro.h"
#include <getopt.h>
#include <unistd.h>
#include "../Utils/Instance3BKP.h"
#include <assert.h>     /* assert */
#include "utils.h"
#include "MasterProblem.h"
#include "SlaveProblem.h"
#include <algorithm>




char OUTPUTFILENAME[128];


using namespace std;

int status;
char errmsg[BUF_SIZE];







/**
 * solve the model write the results in a file and returns the objective value
 * @param env the CPLEX environment
 * @param lp the CPLEX problem
 * @param instance the object encoding of the problem's instance
 * @param oFlag an object containing helpful configuration information passed by cli
 * @param timeout_reached a boolean pointer that is set to true if cplex could not
 * solve the problem within the given timeout (if it is specified)
 * @return objval the optimal solution
 */
double solve( CEnv env, Prob lp, Instance3BKP instance, optionFlag oFlag, bool * timeout_reached, bool master) {
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
		if(master)
			CHECKED_CPX_CALL( CPXsolwrite, env, lp, "/tmp/Model_master.sol");
		else
			CHECKED_CPX_CALL( CPXsolwrite, env, lp, "/tmp/Model_slave.sol");
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
 * fetches the variables' values after the solution is found
 * @param env the CPLEX environment
 * @param lp the CPLEX problem
 * @param instance the problem instance
 * @param map a map containing the CPLEX's indices of the variables
 * @return a VarVal object that contains the values fetched from CPLEX
 */
VarVal fetchVariables(CEnv env, Prob lp, Instance3BKP instance, mapVar map){
	
	int N = instance.N;
	int K = instance.K;
	
	VarVal variable_values(K, N, 6);
	
	{
		vector<double> tmp(K);
		int begin = map.Z[0];
		int end = map.Z[K-1];
		CHECKED_CPX_CALL (CPXgetx, env, lp, &tmp[0], begin, end);
		std::transform(tmp.begin(), tmp.end(), variable_values.z.begin(), [](double val){ return val < 0.5? 0:1; });
	}
	
	
	for(int k = 0; k < K; k++){
		vector<double> tmp(N);
		int begin = map.T[k][0];
		int end = map.T[k][N - 1];
		CHECKED_CPX_CALL( CPXgetx, env, lp, &tmp[0], begin, end);
		std::transform(tmp.begin(), tmp.end(), variable_values.t[k].begin(), [](double val){ return val < 0.5? 0:1; });
	}
	
	for(int k = 0; k < K; k++){
		for(int i = 0; i < N; i++){
			int begin = map.Chi[k][i][0];
			int end = map.Chi[k][i][2];
			CHECKED_CPX_CALL( CPXgetx, env, lp, &variable_values.chi[k][i][0], begin, end);
		}
	}
	
	for(int k = 0; k < K; k++){
		for(int i = 0; i < N; i++){
			for(int j = 0; j < N; j++){
				vector<double> tmp(3);
				int begin = map.B[k][i][j][0];
				int end = map.B[k][i][j][2];
				CHECKED_CPX_CALL( CPXgetx, env, lp, &tmp[0], begin, end);
				std::transform(tmp.begin(), tmp.end(), variable_values.b[k][i][j].begin(), [](double val){ return val < 0.5? 0:1; });
			}
		}
	}
	
	for(int i = 0; i < N; i++){
		vector<double> tmp(6);
		int begin = map.Rho[i][0];
		int end = map.Rho[i][5];
		CHECKED_CPX_CALL( CPXgetx, env, lp, &tmp[0], begin, end);
		std::transform(tmp.begin(), tmp.end(), variable_values.rho[i].begin(), [](double val){ return val < 0.5? 0:1; });
	}
	
	for(int i = 0; i < N; i++){
		for(int r = 0; r < 6; r++){
			if(variable_values.rho[i][r] >= 0.5) { //Taking into account round errors due to the double representation
				for(int delta = 0; delta < 3; delta++){
					variable_values.rotation[i][delta] = instance.R[r][delta];
				}
				break;
			}
		}
	}
	return variable_values;
}

/**
 * Print a human readable output in a file, named filename. This can be also
 * be represented by the webgl written program
 * @param env the CPLEX environment
 * @param lp the CPLEX problem
 * @param instance the instance of the problem
 * @param objval the value of the objective value
 * @param VarVal the values of the fetched variables, computed by the function
 * fetchVariables
 * @param filename the name of the file in which to print the output
 * @param oFlag the configuration of this particular instance
 */
 
void output(CEnv env, Prob lp, Instance3BKP instance, double objval, VarVal v, char * filename, optionFlag oFlag){
	int N = instance.N;
	int K = instance.K;
	
	
	
	ofstream outfile(filename);
	if(!outfile.good()) {
		cerr << filename << endl;
		throw std::runtime_error("Cannot open the file ");
	}
	outfile << "#Valore della funzione obiettivo " << objval << endl; 
	outfile << "#Vengono inclusi solo gli oggetti messi nello zaino" << endl;
	for(int k = 0; k < K; k++){
		if(v.z[k] == 1) {
			outfile << "#Dimensione";
			outfile << " del " << k << "-mo Zaino " << instance.S[k][0] << " " << instance.S[k][1] <<" "<< instance.S[k][2] << endl;
		}
	} 
	
	for(int k = 0; k < K; k++){
		if(v.z[k] == 0)
			continue;
		outfile << "#Knapsack " << k << "-mo" << endl;
		outfile << instance.S[k][0] << " " << instance.S[k][1] << " " << instance.S[k][2] << endl;
		if(oFlag.extended){
			outfile << "L " << instance.L[k][0] << " " << instance.L[k][1] << " " << instance.L[k][2] << endl;
			outfile << "U " << instance.U[k][0] << " " << instance.U[k][1] << " " << instance.U[k][2] << endl;
		}
	
		for(int i = 0; i < N; i++){
			if(v.t[k][i] == 0)
				continue;
			//Print only inserted objects 
			outfile << i << "\t";
			
			for(int delta = 0; delta < 3; delta++){
				outfile << v.chi[k][i][delta];
				outfile << " ";
			}
			outfile << "\t";
			for(int delta = 0; delta < 3; delta++){
				outfile << instance.s[i][v.rotation[i][delta]] << " ";
			}
			
			outfile << endl;
		}
			
	}
	
	outfile.close();
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
	bool timeout_reached[] = {false, false};
	try { 

		optionFlag oFlag = get_option(argc, argv);	
		Instance3BKP instance(oFlag.filename, oFlag.extended);
		DECL_ENV( env );
		DECL_PROB( env, lp );
		
		mapVar map = setupLP(env, lp, instance, oFlag);
		
		/* Set up timeout */
		CHECKED_CPX_CALL(CPXsetintparam, env, CPX_PARAM_CLOCKTYPE, 1); //Use CPU as timer
		CHECKED_CPX_CALL(CPXsetdblparam, env, CPX_PARAM_TILIM, oFlag.timeout);
		
		CHECKED_CPX_CALL(CPXsetintparam, env, CPX_PARAM_THREADS, oFlag.threads);
		
		// find the solution
		double objvalMP = solve(env, lp, instance, oFlag, &timeout_reached[0], true);
		
		if(!oFlag.benchmark)
			printf("Solved Main Problem\n");
		
		
		// fetch variables from the solved model
		VarVal fetched_variables = fetchVariables(env, lp, instance, map);
		
		if(!oFlag.benchmark)
			printf("Fetched variables Successfully\n");
		
		//sprintf(OUTPUTFILENAME, "output_%s%c", oFlag.output_filename, '\0');
		output(env, lp, instance, objvalMP, fetched_variables,  oFlag.output_filename, oFlag);
		
		if(!oFlag.benchmark)
			printf("Written solution for Main Problem in %s\n", oFlag.output_filename);
		
		//We want to optimize at least in one direction
		if(oFlag.optimize[0] || oFlag.optimize[1] || oFlag.optimize[2]){
			double start_chi_value = 0.0;
			double end_chi_value = 0.0;
			
			
			for(int k = 0; k < instance.K; k++){
				for(int i = 0; i < instance.N; i++){
					
						for(int delta = 0; delta < 3; delta++){
							if(oFlag.optimize[delta])
							start_chi_value += fetched_variables.chi[k][i][delta];
						}
					
				}
			}
			if(!oFlag.benchmark)
				cout << "start chi values " << start_chi_value << endl;
			
			
			// Setup Slave Problem
			setupSP(env, lp, instance, map, fetched_variables, oFlag);
			if(!oFlag.benchmark)
				printf("Set up problem Successfully\n");
			
			
			CHECKED_CPX_CALL( CPXwriteprob, env, lp, "/tmp/Model2.lp", NULL ); 
			CHECKED_CPX_CALL(CPXchgprobtype, env, lp, CPXPROB_MILP);
			
			// Solve Slave Problem
			double objvalSP = solve(env, lp, instance, oFlag, &timeout_reached[1], false); 
			
			
			
			VarVal slave_variables = fetchVariables(env, lp, instance, map);
			
		
			
			
			
			end_chi_value = 0;
			for(int k = 0; k < instance.K; k++){
				for(int i = 0; i < instance.N; i++){
					
						for(int delta = 0; delta < 3; delta++){
							if(oFlag.optimize[delta])
							end_chi_value += slave_variables.chi[k][i][delta];
						}
					
				}
			}
			if(!oFlag.benchmark)
				cout << "end chi values " << end_chi_value << endl;
			
			sprintf(OUTPUTFILENAME, "%s2", oFlag.output_filename);

			output(env, lp, instance, objvalSP,  slave_variables,  OUTPUTFILENAME, oFlag);
			if(!oFlag.benchmark)
				printf("Written solution for Extra Problem in %s\n", OUTPUTFILENAME);
			
		}
		// free-allocated resources
		CPXfreeprob(env, &lp);
		CPXcloseCPLEX(&env);
	} catch (exception& e){
		cerr << ">>Exception in processing " << argv[1] << ": " << e.what() << endl;
		exc_arised = true;
	}
	if(exc_arised){
		return EXIT_FAILURE;
	} 
	if(timeout_reached[0]){
		return 124;
	}
	return EXIT_SUCCESS;
}
