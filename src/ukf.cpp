#include "ukf.h"
#include "tools.h"
#include "Eigen/Dense"
#include <iostream>

using namespace std;
using Eigen::MatrixXd;
using Eigen::VectorXd;
using std::vector;

/**
 * Initializes Unscented Kalman filter
 */


UKF::UKF() {
  
  is_initialized_ = false;

  previous_timestamp_ = 0;
  
  // if this is false, laser measurements will be ignored (except during init)
  use_laser_ = true;

  // if this is false, radar measurements will be ignored (except during init)
  use_radar_ = true;

  // initial state vector
  x_ = VectorXd(dim_x);

  // Allocating state covariance matrix
  Sigma_ = MatrixXd(UKF::dim_x, UKF::dim_x);
  // These are the sigma points generated by the joint distribution 
  // of the prior and the (linear acceleration, angular accelration) distribution.
  // Thus, the dimension is dim_x_joint x (2*dim_x_joint+1)
  Xsig_joint_ = MatrixXd(dim_x_joint, 2 * dim_x_joint + 1);
  
  // These are the sigma points that yield the predicted state distribution.
  // They are obtained from the joint sigma points, so, although the rows should be x_dim, 
  // the nmumber of columns should be equal to the number of columns of Xsig_joint (i.e., 2 * dim_x_joint + 1) 
  Xsig_pred_ = MatrixXd(dim_x, 2 * dim_x_joint + 1);

  // Process noise standard deviation longitudinal acceleration in m/s^2
  std_a_ = .5;

  // Process noise standard deviation yaw acceleration in rad/s^2
  std_yawdd_ = .05;

  // Laser measurement noise standard deviation position1 in m
   //std_laspx_ = 0.15; // initial variance estimate
  std_laspx_ = 0.015;

  // Laser measurement noise standard deviation position2 in m
   //std_laspy_ = 0.15; // initial variance estimate
  std_laspy_  = 0.015;
  
  // Radar measurement noise standard deviation radius in m
  std_radr_ = 0.03;

  // Radar measurement noise standard deviation angle in rad
  std_radphi_ = 0.003;

  // Radar measurement noise standard deviation radius change in m/s
  std_radrd_ = 0.03;

  /**
  TODO:

  Complete the initialization. See ukf.h for other member properties.

  Hint: one or more values initialized above might be wildly off...
  */
}

UKF::~UKF() {}


// Initialization of the prior 
// either from a Lidar or a Radar measurement.
void UKF::Init(const MeasurementPackage &pack) {
	// preliminary allocation
	x_ = VectorXd(dim_x);
	
	if (pack.sensor_type_ == MeasurementPackage::LASER) { // direct initialization from the Lidar data
	  
	  //cout<<"Initial LIDAR measurement : "<<endl<<pack.raw_measurements_<<endl;
	  double px = pack.raw_measurements_[0], 
		 py = pack.raw_measurements_[1];
	  
	  if (px*px + py*py == 0) { // initialize prior with high uncertainty
	    x_ << 0,
		  0,
		  0,
		  0,
		  0;
	    Sigma_ = UKF::inf_variance * MatrixXd::Identity(dim_x, dim_x);
	  } 
	  else { 
	    // state mean
	    x_<< px, 
		 py,
		  0, 
	          0,
		  0;
	
	  // state covariance
	  Sigma_ = MatrixXd(dim_x, dim_x);
	  Sigma_ << std_laspx_ * std_laspx_ , 0 , 0, 0 , 0,
		    0.0, std_laspy_ * std_laspy_, 0 , 0, 0,
		     0        ,         0       , UKF::inf_variance, 0, 0,
		     0        ,         0       , 0, UKF::inf_variance, 0,
		     0        ,         0       ,       0             , UKF::inf_variance;
	  }
	  
	} else { // Oh-oh... Radar measurement...
	 
	  // obtain px, py implicitly
	  double rho = pack.raw_measurements_[0];
	  //cout<<"Initial RADAR measurement : "<<endl<<pack.raw_measurements_<<endl;
	  if (rho == 0) { // initialize the prior with high uncertainty around zero...
	    x_ <<  0, 
		   0,
		   0,
		   0;
	    Sigma_ = UKF::inf_variance * MatrixXd::Identity(dim_x, dim_x);
	  } 
	  else {
	    // NOTE: θ (theta) now is the angle of the position vector with the x-axis of the sensor
	    
	    double theta = pack.raw_measurements_[1];
	    double px = rho * cos(theta);
	    double py = rho * sin(theta);
	  
	    // get rho_dot:
	    double rho_dot = pack.raw_measurements_[2];
	    // Now, we know that, since rho_dot = (vx * px + vy * py) / norm(p):
	    // norm(v) = abs(rho_dot):
	    // So,
	    double v = fabs(rho_dot); 
	   
	    // NOTE: Now, we can extract a distribution on p and v based on the measurement,
	    //       but NOT on phi and omega. That's not too bad...
	    // So, we get sigma points from the measurement
	    const int n_y = 3;// numnber of measurements in first measuerement vector
	    int lambda = 3 - n_y;
	    
	    MatrixXd Ysigma(n_y, 2 * n_y + 1);
	    MatrixXd Qr(n_y, n_y);
	    Qr << std_radr_ * std_radr_,             0,                        0, 
	             0                 , std_radphi_ * std_radphi_,            0,
		     0                 ,             0            , std_radrd_ * std_radrd_;
	    GenerateSigmaPoints(pack.raw_measurements_, 
				Qr,
			        Ysigma);
	    // Great! Now we need to transform the sigma points (and compute the weighhts by the way....)
	    MatrixXd Tsigma(n_y, 2 * n_y + 1); // transformed points
	    double w[2 * n_y + 1]; // weights
	    for (int i = 0; i < 2 * n_y + 1; i++) {
	      
	      w[i] = i == 0 ? lambda / (lambda + n_y) : 0.5 / (lambda + n_y);
	      double rho_t = Ysigma(0, i);
	      double theta_t = Ysigma(1, i);
	      double rho_dot_t = Ysigma(2, i);
	      double px_t = rho_t * cos(theta_t);
	      double py_t = rho_t * sin(theta_t);
	      double v_t = fabs(rho_dot_t);
	      
	      Tsigma(0, i) = px_t;
	      Tsigma(1, i) = py_t;
	      Tsigma(2, i) = v_t;
	      
	    }
	    // -Sigh-! Now compute the mean and covariance of [px, py, v]
	    VectorXd x3 = VectorXd::Zero(n_y);
	    for (int i = 0; i < 2 * n_y + 1; i++) 
	      x3 += w[i] * Tsigma.col(i);
	      
	    
	    MatrixXd Sigma3 = MatrixXd::Zero(n_y, n_y);
	    for (int i = 0; i < 2 * n_y + 1; i++) 
	      Sigma3 += w[i] * (Tsigma.col(i) - x3) * (Tsigma.col(i) - x3).transpose();
	      
	    
	    // AND FINALLY, create the prior
	    x_.segment<3>(0) = x3;
	    x_[n_y] = x_[n_y+1] = 0; // just set phi and omega to zero with lots of variance
			       // this is not too bad an assumption...
	    Sigma_ = MatrixXd::Zero(dim_x, dim_x);
	    Sigma_.block<n_y, n_y>(0, 0) = Sigma3;
	    // setting the variance of phi and omega to "infinity" because we dont anything about them just yet....
	    Sigma_(n_y, n_y) = Sigma_(n_y+1, n_y+1) = inf_variance;
	    // ALL DONE!
	    
	  
	  }
	}
	  
	// Done 
}

/**
 * @param {MeasurementPackage} meas_package The latest measurement data of
 * either radar or laser.
 */
void UKF::ProcessMeasurement(const MeasurementPackage &pack) {
   // if this is the first reception, use the measurement to initialize the prior
  if (!is_initialized_) {
	// initialize the filter
	Init(pack);
	// forward timestamp cache
	previous_timestamp_ = pack.timestamp_;
	// raise the flag so that we skip this part next time
	is_initialized_ = true; 
	return;
  }

  // Compute time since last sample
  float dt = (pack.timestamp_ - previous_timestamp_) / 1000000.0;	//dt - expressed in seconds
  // Forward sampling time 
  previous_timestamp_ = pack.timestamp_;

  
  	 
  //1. Get the posterior marginal over the previous state
  Prediction(dt);

  //2. Now update the posterior
  Update(pack);
  
  // print the moments of state distribution
  cout << "x_= " << x_ << endl;
  cout << "Sigma_= " << Sigma_ << endl;


 
}

/**
 * Predicts sigma points, the state, and the state covariance matrix.
 * @param {double} delta_t the change in time (in seconds) between the last
 * measurement and this one.
 */
void UKF::Prediction(double Dt) {
  // Now generating siugma points for the JOINT distribution of 
  // x-plus the linear - angular accelerations
  GenerateJointSigmaPoints(x_, Sigma_, 
			   std_a_,
			   std_yawdd_,
			   Xsig_joint_);
  
  
  // 1. Now transforming sigma points
  const int dim_joint = Xsig_joint_.rows();
  const int n_joint_sigma_points = Xsig_joint_.cols();
  double lambda = 3 - dim_joint; // the lambda param
  
  double alpha_sq = (lambda + dim_joint) / (dim_joint + kappa);
  
  double w_m[n_joint_sigma_points ];   // mean weights
  double w_c[n_joint_sigma_points ];   // covariance weights
  
  // zeroing the predicted mean
  VectorXd x_pred = VectorXd::Zero(dim_x);
  
  //predict sigma points
  for (int i = 0; i < n_joint_sigma_points ; i++) {
	
      // btw, compute the weights here...
     if (i == 0) {
	
	w_m[i] = lambda / (lambda + dim_joint);
	w_c[i] = w_m[i] + (1 - alpha_sq + beta);
      
      } else 
	w_m[i] = w_c[i] = 0.5 / (lambda + dim_joint);
      
      // Now run sigma points through the transition/prediction equations
      double px = Xsig_joint_(0, i),
             py = Xsig_joint_(1, i),
             v = Xsig_joint_(2, i),
             // NOTE: "phi" instead of "psi"
             phi = Xsig_joint_(3, i),
             omega = Xsig_joint_(4, i),
             a = Xsig_joint_(5, i),
             omega_dot = Xsig_joint_(6, i);
	     
      // Computing the "noise", which is presumed the same in either liner or mixed-rotational motion
      double px_noise = 0.5 * Dt * Dt * cos(phi) * a,
             py_noise = 0.5 * Dt * Dt * sin(phi) * a,
             v_noise = Dt * a,
             phi_noise = 0.5 * Dt * Dt * omega_dot,
             omega_noise = Dt * omega_dot;
    
      // the following updates are common to either linear or general motion
      Xsig_pred_(2, i) = v   +                  + v_noise;
      Xsig_pred_(4, i) = omega                  + omega_noise;
      
      if (fabs(omega) < 0.001) {
      
	  Xsig_pred_(0, i) = px + v * cos(phi) * Dt + px_noise;
	  Xsig_pred_(1, i) = py + v * sin(phi) * Dt + py_noise;
	  Xsig_pred_(3, i) = phi                    + phi_noise;
    
      } else {
      
	  Xsig_pred_(0, i) = px + v * (sin(phi + omega * Dt) - sin(phi) ) / omega   + px_noise;
	  Xsig_pred_(1, i) = py + v * (-cos(phi + omega * Dt) + cos(phi) ) / omega   + py_noise;
	  Xsig_pred_(3, i) = phi + omega * Dt       				     + phi_noise;
      }
      
      x_pred += w_m[i] * Xsig_pred_.col(i);
  
  }
  
  // predicted covariance  
  MatrixXd Sigma_pred = MatrixXd::Zero(dim_x, dim_x);
  for (int i = 0; i < n_joint_sigma_points; i++)
    Sigma_pred += w_c[i] * (Xsig_pred_.col(i) - x_pred) * (Xsig_pred_.col(i) - x_pred).transpose();
  
  // assigning values
  x_ = x_pred;
  Sigma_ = Sigma_pred;
  // all done with prediction mesa thinks...

}



void UKF::Update(const MeasurementPackage& pack) {
  
  if (pack.sensor_type_ == MeasurementPackage::LASER) {
    
    if (use_laser_) {
      NIS_laser_ = UKF::UpdateLidar(pack,
				    Xsig_pred_,
				    x_,
				    Sigma_,
				    std_laspx_,
				    std_laspy_,
				    beta,
				    kappa
				    );
    //std::cout <<"Laser NIS : "<<NIS_laser_<<endl;
      
    }
  }
  else if (pack.sensor_type_ == MeasurementPackage::RADAR) {// oh-oh...
    if (use_radar_) {
      VectorXd x_temp;
      MatrixXd Sigma_temp;
      NIS_radar_ = UKF::UpdateRadar(pack,
				    Xsig_pred_,
				    x_,
				    Sigma_,
				    std_radr_,
				    std_radphi_,
				    std_radrd_,
				    x_temp,
				    Sigma_temp,
				    beta,
				    kappa
				    );
      //std::cout <<"Radar NIS : "<<NIS_radar_<<endl;
      // for now just update the current state moments
      x_ = x_temp;
      Sigma_ = Sigma_temp;
    }
    
  }
}
