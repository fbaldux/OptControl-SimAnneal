/*  -------------------------------------------------------------------------------------------  //

    This program anneals a random configuration of Ising spins s[i]= +-1, according to the 
    cost function

        H = sum_ij J[i,j] s[i] s[j] - log |sum_i h[i] s[i]| - K sum_i s[i] s[i+1]

    The variables J[i,j] and h[i] are loaded from a file.
    The energy is computed efficiently at each step.
    The configurations found are saved to a file, with the # of pulses and 1/eta in the header. 
    
//  -------------------------------------------------------------------------------------------  */


#include <iostream>
#include <cstdio>
#include <iomanip>
#include <fstream>
#include <cmath>
#include <random>

using namespace std;

// random number generator
random_device seed; // obtain a random number from hardware
mt19937 generator(seed()); // seed the generator (Mersenne twister)
uniform_int_distribution<> randomBit(0,1);
uniform_real_distribution<> randomReal(0.0,1.0);

// struct definition
struct EStruct {
  double J;
  double h;
  double K;
  double tot;
};

// fixed variables
#define lambda 1.       // for the exponential temperature ramp
#define gyro 28025      // to compute eta

// flux control
#define load_Jh

// global variables
int N, tone, harmonic, ann_steps, MC_steps, MC_ramp_step; 
double T0, K0;
double *Js, *hs;


//  --------------------------------------  load h and J  -------------------------------------  //

void load_J() {
    char filename[100];
    snprintf(filename, 100, "Init/J_%dspins_%dus.txt", N, N*160/1000);
    ifstream infile(filename);
        
    if ( ! infile.is_open() ) {
        cerr << "\nError! No input file.\n\n" << endl;
        exit(-1);
    }
    
    // skipping the header
    //infile.ignore(1000,'\n');
    
    int i=0;
    double Ji;
    while (infile >> Ji) {
        Js[i++] = Ji;
    }
    
    infile.close();
}


void load_h() {
    char filename[100];
    if (tone == 3)
        snprintf(filename, 100, "Init/h_%dtone_%dspins_%dus.txt", tone, N, N*160/1000);
    else if (tone == 1)
        snprintf(filename, 100, "Init/h_%dtone_%dharm_%dspins_%dus.txt", tone, harmonic, N, N*160/1000);
    
    ifstream infile(filename);
    
    if ( ! infile.is_open() ) {
        cerr << "\nError! No input file.\n\n" << endl;
        exit(-1);
    }
    
    // skipping the header
    //infile.ignore(1000,'\n');
    
    int i=0;
    double hi;
    while (infile >> hi) {
        hs[i++] = hi;
    }

    infile.close();
}


//  -----------------------------------  copy configuration  ----------------------------------  //
// copies s1 into s2
void copy_s(int *s1, int *s2) {
    for (int i=0; i<N; i++)
        s2[i] = s1[i];
}


//  -------------------------------------  cost function  -------------------------------------  //

EStruct energy(int *s){
    EStruct E;
    E.J=0.; E.h=0.; E.K=0.;
    
    for (int i=0; i<N; i++) {
        E.h += hs[i]*s[i];
        
        for (int j=0; j<i; j++) {
            E.J += 2.*Js[i*N+j]*s[i]*s[j];
        }
        // also the diagonal term matters
        E.J += Js[i*N+i];
    }
    
    for (int i=1; i<N; i++) {
        E.K += K0*s[i-1]*s[i];
    }

    E.tot = E.J - log(abs(E.h)) - E.K;
  
    return E;
} 


EStruct new_energy(int *s, int flip, EStruct E){
    EStruct E_new = E;
    
    // h term
    E_new.h -= 2*hs[flip]*s[flip];
    
    // J term
    for (int i=0; i<flip; i++)
        E_new.J -= 4*Js[i*N+flip]*s[i]*s[flip];
    for (int i=flip+1; i<N; i++){
        E_new.J -= 4*Js[i*N+flip]*s[i]*s[flip];
    }
    
    // K term
    if (flip>0) {
        if (flip<N-1)
            E_new.K -= 2*K0*s[flip]*(s[flip-1]+s[flip+1]);
        else
            E_new.K -= 2*K0*s[flip]*s[flip-1];
    }
    else
        E_new.K -= 2*K0*s[flip]*s[flip+1];
    
    E_new.tot = E_new.J - log(abs(E_new.h)) - E_new.K;

    return E_new;
} 


//  ------------------------------------  temperature ramp  -----------------------------------  //

double temperature(int n) {
    // exponential ramp
    //return T0*exp(-lambda*(double)n/ann_steps);
    
    // power-law ramp
    //return T0*(1.-(double)n/ann_steps);
    //return T0*(1.-(double)n/ann_steps)*(1.-(double)n/ann_steps);
    //return T0**(1.-(double)n/ann_steps)*(1.-(double)n/ann_steps)*(1.-(double)n/ann_steps);
    //return T0**(1.-(double)n/ann_steps)*(1.-(double)n/ann_steps)*(1.-(double)n/ann_steps)*(1.-(double)n/ann_steps);

    // inverse-power-law ramp
    return T0/(1.+lambda * n * T0);

    // from arXiv:2102.00182
    //return max( 0.5/asinh(tan(0.5*M_PI*n/ann_steps)), 0.);
}


//  ---------------------------------  acceptance probability  --------------------------------  //

double acceptance_prob(double deltaE, double T) {
    if (deltaE<0.)
        return 1.;
    else
        return exp(-deltaE/T);
}


//  ------------------------------------  annealing cycle  ------------------------------------  //

void anneal(int *s, int *best_s, EStruct *E){
    
    // initial energy
    *E = energy(s);
    double best_E = E->tot;
    
    // gradually decrease the temperature
    for (int n=1; n<ann_steps; n++) {
        
        double T = temperature(n);
        
        // equilibrate at fixed temperature
        for (int t=0; t<MC_steps; t++) {
        
            // flip a spin in s_new
            int flip = randomReal(generator)*N;
        
            // find the new energy       
            EStruct E_new = new_energy(s, flip, *E);
        
            // assess the move
            double coin = randomReal(generator);
            if (coin<acceptance_prob(E_new.tot-E->tot,T)) {
                s[flip] = -s[flip];
                *E = E_new;
            }
        
            // keep track of the best configuration so far
            if (E->tot < best_E) {
                best_E = E->tot;
                copy_s(s, best_s);
            }
        }
    }
    
    *E = energy(best_s);
}


//  ---------------------------------  analyze configuration  ---------------------------------  //

// count the domain walls
int domain_walls(int *s) {
    int count=0;
    for (int i=1; i<N; i++)
        count += s[i]*s[i-1];
  
    return (N-count)/2;
}


// sensitivity
double etaInv(double epsilon) {
    return 1./exp(epsilon - log(gyro)-0.5*log(N*0.16*1e-6));
}


//  ------------------------------------  save output data  -----------------------------------  //

void save_s(int *s, int dw, double this_etaInv, int r) {
    // create the output file
    char filename[100];
    if (tone == 3)
        snprintf(filename, 100, "Configurations/s_%dtone_%dspins_%dus_K%.4f_r%d.txt", tone, N, N*160/1000, K0, r);
    else if (tone == 1)
        snprintf(filename, 100, "Configurations/s_%dtone_%dharm_%dspins_%dus_K%.4f_r%d.txt", tone, harmonic, N, N*160/1000, K0, r);        
    ofstream outfile(filename);
    
    if ( ! outfile.is_open() ) {
        cerr << "\nError with the output file!\n\n" << endl;
        exit(-1);
    }

    // write the header
    outfile << "# pulses=" << dw << ", 1/eta=" << this_etaInv << endl;
    // write the rest
    for (int i=0; i<N; i++) {
        outfile << s[i] << endl;
    }
    
    outfile.close();
}


//  ------------------------------------------  main  -----------------------------------------  //

int main( int argc, char *argv[] ) {
    int Reps, *s, *best_s;
    EStruct E;
    
    // parameter acquisition
    if( argc != 9 ) {
        cerr << "\nError! Usage: ./SA <N> <tone> <harmonic> <ann_steps> <MC_steps> <Temp0> <K> <Reps>\n\n";
        exit(-1);
    }
    N = strtod(argv[1], NULL);
    tone = strtod(argv[2], NULL);
    harmonic = strtod(argv[3], NULL);
    ann_steps = strtod(argv[4], NULL);
    MC_steps = strtod(argv[5], NULL);
    T0 = strtof(argv[6], NULL);
    K0 = strtof(argv[7], NULL);
    Reps = strtod(argv[8], NULL);
           
    // dynamic allocation
    Js = new double[N*N]();
    hs = new double[N]();
    s = new int[N]();
    best_s = new int[N]();
    
    if (Js==NULL || hs==NULL || s==NULL|| best_s==NULL) {
        cerr << "\nError! Memory allocation failed.\n\n";
        exit(-1);
    }
    
    // load J and h
#ifdef load_Jh
    load_J();
    load_h();
#endif
    // TEMPORARY: extract random J and h
#ifndef load_Jh
    for (int i=0; i<N; i++) {
        hs[i] = randomReal(generator)*2. - 1.;
        
        for (int j=0; j<i; j++) {
            Js[i*N+j] = (randomReal(generator)*2. - 1.)/N;
            Js[j*N+i] = Js[i*N+j];
        }
    }
#endif
    
    
    // create the output file
    char filename[100];
    if (tone == 3)
        sprintf(filename, "Results/%dtone_%dspins_%dus_K%.4f.txt", tone, N, N*160/1000, K0);
    else if (tone == 1)
        sprintf(filename, "Results/%dtone_%dharm_%dspins_%dus_K%.4f.txt", tone, harmonic, N, N*160/1000, K0);
    FILE *outfile = fopen(filename, "w");  
    fprintf(outfile, "# N=%d, MC_steps=%d, T0=%f, K=%f\n# pulses 1/eta\n", N, MC_steps, T0, K0);
    
    
    // perform many annealing cycles
    for (int r=0; r<Reps; r++) {
        // initial random state
        for (int i=0; i<N; i++) {
            s[i] = randomBit(generator)*2 - 1;
        }

        // anneal the state s to best_s
        anneal(s, best_s, &E);
        
        // save data
        int dw = domain_walls(best_s);
        double this_etaInv = etaInv(E.tot+E.K);
        fprintf(outfile, "%d %e\n", dw, this_etaInv);
        // save configuration to file
        save_s(best_s, dw, this_etaInv, r);
        
        fflush(0);
    }
    
    // close the files 
    fclose(outfile);
    
    
    // free the memory
    free(Js);
    free(hs);
    free(s);
    free(best_s);
    
    return 0;
}
