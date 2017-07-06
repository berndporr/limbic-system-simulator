#include "isolearning.h"

#include <complex>

/**
 * get a pointer to a filter
 **/
Trace* Isolearning::getFilter(int channel, int filterindex) {
	return iir[channel][filterindex];
}


void Isolearning::setInput(int i,float f) {
	lastInputs[i]=inputs[i];
	inputs[i]=f;
}



/**
 * init the bandpass filters and set all weights to zero
 **/
Isolearning::Isolearning(int nInputs, 
			 int nFi) {
	fprintf(stderr,"ISO init: # of channels = %d, # of filters = %d\n",
		nInputs,nFi);
	nChannels=nInputs;
	inputs=new float[nInputs];
	lastInputs=new float[nInputs];
	for(int i=0;i<nInputs;i++) {
		inputs[i]=0.0;
		lastInputs[i]=0.0;
	}
	nFilters=nFi;
	learningRate=0.000001; // this should work initially
	thirdFactor=1.0;
	decay=0.0F;
	delta=0.0F;
	actualActivity=0.0F;
	SumAutoCorr=0.0F;     //Auto correlation term
	deltaAutoCorr=0.0F;
        Unot=0.0F;
        deltaUnot=0.0F;
        lastUnot=0.0F;
        weightdeltaUnot=0.0F;
	fprintf(stderr,"Init filters\n");
	iir=new (Trace**)[nChannels];
	for(int j=0;j<nChannels;j++) {
		iir[j]=new (Trace*)[nFilters];
		for(int i=0;i<nFilters;i++) {
			iir[j][i]=new Trace();
		}
	}
	fprintf(stderr,"Init weights\n");
	weight=new (float*)[nChannels];
	for(int j=0;j<nChannels;j++) {
		weight[j]=new float[nFilters];
		for(int i=0;i<nFilters;i++) {
			weight[j][i]=0.0;
		}
	}
	fprintf(stderr,"Init the last corr\n");
	lastCorrel=new (float*)[nChannels];
	for(int j=0;j<nChannels;j++) {
		lastCorrel[j]=new float[nFilters];
		for(int i=0;i<nFilters;i++) {
			lastCorrel[j][i]=0.0;
		}
	}
	
	/**
	 * setting the weight of the reflex to a initial level (as a reference)
	 **/
	weight[0][0]=1.0;
	fOutput=NULL;
	fWeights=NULL;
	fFilter=NULL;
	fweightFilter=NULL;
	doDocu=0;
	onlyChange=0;
	setReflex(0.01,0.6);
}


Isolearning::~Isolearning() {
	if (doDocu) {
		fclose(fOutput);
		fclose(fWeights);
		fclose(fFilter);
		fclose(fweightFilter);
	}
	delete inputs;
	for(int j=0;j<nChannels;j++) {
		for(int i=0;i<nFilters;i++) {
			delete iir[j][i];
		}
		delete iir[j];
	}
	delete iir;
	for(int j=0;j<nChannels;j++) {
		delete weight[j];
	}
	delete weight;
	for(int j=0;j<nChannels;j++) {
		delete lastCorrel[j];
	}
	delete lastCorrel;
}



void Isolearning::openDocu(char* ndocu) {
	char tmp[128];
	sprintf(tmp,"%s.log",ndocu);
	char nWeights[128];
	sprintf(nWeights,"%s_weights.dat",ndocu);
	fprintf(stderr,"Opening %s for the weights\n",nWeights);
	fWeights=fopen(nWeights,"wt");
	if (!fWeights) {
		fprintf(stderr,"Could not open docu %s\n",nWeights);
		exit(1);
	}

	char nFilter[128];
	sprintf(nFilter,"%s_filter.dat",ndocu);
	fprintf(stderr,"Opening %s for the Filters\n",nFilter);
	fFilter=fopen(nFilter,"wt");
	if (!fFilter) {
		fprintf(stderr,"Could not open docu %s\n",nFilter);
		exit(1);
	}



	char weightFilter[128];
	sprintf(weightFilter,"%s_filters_weighted.dat",ndocu);
	fprintf(stderr,"Opening %s for the Filters\n",nFilter);
	fweightFilter=fopen(weightFilter,"wt");
	if (!fweightFilter) {
		fprintf(stderr,"Could not open docu %s\n",weightFilter);
		exit(1);
	}



	char nOutput[128];
	sprintf(nOutput,"%s_output.dat",ndocu);
	fprintf(stderr,"Opening %s for the output\n",nOutput);
	fOutput=fopen(nOutput,"wt");
	if (!fOutput) {
		fprintf(stderr,"Cound not open docu %s\n",nOutput);
		exit(1);
	}
	doDocu=1;
}




float Isolearning::calcFrequ(float f0,int i) {
	return f0/(i+1);
	return f0;
}



void Isolearning::setReflex(float f0, // frequency
			    float q0  // quality
			    ) {
	getFilter(0,0)->calcCoeffBandp(f0,q0);
	getFilter(0,0)->impulse("h0.dat");
}


void Isolearning::setPredictorsAsBandp(float f1,
				       float q1
				       ) {
	for(int i=1;i<nChannels;i++) {
		for(int j=0;j<nFilters;j++) {
			float fi=calcFrequ(f1,j);
			fprintf(stderr,"Bandp channel %d,filter #%d: f=%f, Q=%f\n",
				i,j,fi,q1);
			getFilter(i,j)->calcCoeffBandp(fi,q1);
			char tmp[256];
			sprintf(tmp,"h%d_%02d.dat",i,j);
			getFilter(i,j)->impulse(tmp);
		}
	}
}


void Isolearning::setPredictorsAsTraces(int nTaps,
					float tau,
					int n) {
	for(int i=1;i<nChannels;i++) {
		for(int j=0;j<nFilters;j++) {
			getFilter(i,j)->calcCoeffTrace((int)(((float)nTaps)/((float)(j+1))),
						       tau/((float)(j+1)),
						       n); // not normalised
			char tmp[256];
			sprintf(tmp,"h%d_%02d.dat",i,j);
			getFilter(i,j)->impulse(tmp);
		}
	}
}


void Isolearning::prediction(int) {
	/**
	 * Feed the inputs into the filter bank
	 **/

	// channel 0 is no filter bank, it's just one filter
	getFilter(0,0)->filter(inputs[0]);

	// feed the input-channels into the Filter filters
	for(int j=1;j<nChannels;j++) {
		// look throught the filter bank
		for(int i=0;i<nFilters;i++) {
			// get an input
			float dd=inputs[j];
			if (onlyChange) {
				// filter out the DC
				dd=inputs[j]-lastInputs[j];
			}
			// feed it into the filter
			getFilter(j,i)->filter(dd);
		}
	}

	//the weight * the derivative of filter output U0
	weightdeltaUnot=getWeight(0,0)*deltaUnot; 

	/**
	 *get filter output U0 term and its Derivative
	 **/
	
	Unot= getFilter(0,0)->getActualOutput();
	deltaUnot=Unot-lastUnot; //Derivative of the filter output
	lastUnot=Unot;
	//the weight * the derivative of filter output U0
	weightdeltaUnot=getWeight(0,0)*deltaUnot; 
	

	/**
	 * calculate the new activity: weighted sum over the filters
	 **/
	// get the reflex
	actualActivity=getFilter(0,0)->getActualOutput();
	// loop through the predictive inputs
	for(int j=1;j<nChannels;j++) {
		for(int i=0;i<nFilters;i++) {
			actualActivity=actualActivity+
			  getFilter(j,i)->getActualOutput()*getWeight(j,i);
		}
	}
	
	// Calculate the derivative of the output
	delta=actualActivity-lastActivity;
	lastActivity=actualActivity;
  	/**if (actualActivity > 1.0) 
	  {
	    actualActivity = 10.0;
	    }**/
	/**
	 *Calculate the sum of the auto corr term
	 **/
	//// loop through the predictive inputs
	SumAutoCorr=0.0F;
	for(int j=1;j<nChannels;j++) {
		for(int i=0;i<nFilters;i++) {
			SumAutoCorr=SumAutoCorr+
				getWeight(j,i)*getFilter(j,i)->getActualOutput();
		}
	}
 
	// calculate the derivative of the sum of the autocorrelation term
	deltaAutoCorr=SumAutoCorr-lastAutoCorr;
	lastAutoCorr=SumAutoCorr;
	/**
	 * change the weights
	 **/
	// run through different predictive input channels
	for(int j=1;j<nChannels;j++) {
		// run through the filter bank
		for(int i=0;i<nFilters;i++) {
			// ISO learning with 3rd factor
		  //  float correl=thirdFactor*delta*getFilter(j,i)->getActualOutput()
		  //   -decay*getWeight(j,i)*getFilter(j,i)->getActualOutput();

		  /** THE ORIGINAL REVERSAL**/
		  /**float correl=thirdFactor*delta*getFilter(j,i)->getActualOutput()
		      -decay*getWeight(j,i)*fabs(getFilter(j,i)->getActualOutput())
		      *fabs(delta); **/
		  
		  float correl=thirdFactor*delta*getFilter(j,i)->getActualOutput()
		    -decay*getWeight(j,i)*fabs(getFilter(j,i)->getActualOutput())
		      *fabs(delta);
	
		  // calculate the integral by a linear approximation
			float integral=correl-(correl-lastCorrel[j][i])/2.0;
			// set the weight
			setWeight(j,i,
				  getWeight(j,i)+0.00001*integral);
			// save the last correlation result. Needed for the
			// integral.
			lastCorrel[j][i]=correl;
		}
	}
}

/**
 * Write gnuplot friendly output
 **/
void Isolearning::writeDocu(int step) {
	if (!doDocu) return;

	fprintf(fOutput,"%d %f %f\n",step,actualActivity,delta);
	
	// weights
	fprintf(fWeights,"%d",step);
	for(int i=0;i<nChannels;i++) {
		for(int j=0;j<nFilters;j++) {
			fprintf(fWeights," %f",getWeight(i,j));
			//fprintf(stderr," %d",j);
		}
	}
	fprintf(fWeights,"\n");
	fflush(fWeights);

	// Filter outputs
	fprintf(fFilter,"%d",step);
	for(int i=0;i<nChannels;i++) {
		for(int j=0;j<nFilters;j++) {
			fprintf(fFilter," %f",getFilter(i,j)->getActualOutput());
		}
	}
	fprintf(fFilter,"\n");
	fflush(fFilter);

	// weighted Filter outputs
	fprintf(fweightFilter,"%d",step);
	for(int i=0;i<nChannels;i++) {
		for(int j=0;j<nFilters;j++) {
			fprintf(fweightFilter," %f",
				(getFilter(i,j)->getActualOutput())*getWeight(i,j)
				);
		}

	   
	}
	fprintf(fweightFilter,"\n");
	fflush(fweightFilter);
}