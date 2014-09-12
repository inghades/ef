#include "domain.h"

// Domain initialization
void domain_particles_init( Domain *dom, Config *conf );
// Pic algorithm
void domain_prepare_leap_frog( Domain *dom );
void domain_advance_one_time_step( Domain *dom );
void eval_charge_density( Domain *dom );
void eval_potential_and_fields( Domain *dom );
void push_particles( Domain *dom );
void apply_domain_constrains( Domain *dom );
void update_time_grid( Domain *dom );
// Eval charge density on grid
void clear_old_density_values( Domain *dom );
void weight_particles_charge_to_mesh( Domain *dom );
void next_node_num_and_weight( const double x, const double grid_step, int *next_node, double *weight );
// Eval fields from charges
void solve_poisson_eqn( Domain *dom );
extern "C" void hwscrt_( double *, double *, int *, int *, double *, double *,
			 double *, double *, int *, int *, double *, double *,
			 double *, double *, int *, double *, int *, double * );
void rowmajor_to_colmajor( double **c, double *fortran, int dim1, int dim2 );
void colmajor_to_rowmajor( double *fortran, double **c, int dim1, int dim2 );		  
void hwscrt_init_f( double left, double top, 
		    double right, double bottom, 
		    double **charge_density, double **hwscrt_f);
double **poisson_init_rhs( Spatial_mesh *spm );
void poisson_free_rhs( double **rhs, int nrow, int ncol );
void eval_fields_from_potential( Domain *dom );
double boundary_difference( double phi1, double phi2, double dx );
double central_difference( double phi1, double phi2, double dx );
// Push particles
void leap_frog( Domain *dom );
void shift_velocities_half_time_step_back( Domain *dom );
void update_momentum( Domain *dom, double dt );
void update_position( Domain *dom, double dt );
Vec2d force_on_particle( Domain *dom, int particle_number );
// Boundaries and generation
void apply_domain_boundary_conditions( Domain *dom );
bool out_of_bound( Domain *dom, Vec2d r );
void remove_particle( int *i, Domain *dom );
void proceed_to_next_particle( int *i, Domain *dom );
// Domain print
char *construct_output_filename( const char *output_filename_prefix, 
				 const int current_time_step,
				 const char *output_filename_suffix );
// Various functions
void domain_print_particles( Domain *dom );





//
// Domain initialization
//

Domain::Domain( Config *conf ) :
  time_grid( Time_grid( conf ) ),
  spat_mesh( Spatial_mesh( conf ) )
{
    config_check_correctness( conf );
    //
    domain_particles_init( this, conf );
    return;
}

void domain_particles_init( Domain *dom, Config *conf )
{
    particles_test_init( &(dom->particles), &(dom->num_of_particles), conf );
    return;
}

//
// Pic simulation 
//

void domain_run_pic( Domain *dom, Config *conf )
{
    int total_time_iterations, current_node;
    total_time_iterations = dom->time_grid.total_nodes - 1;
    current_node = dom->time_grid.current_node;
    
    domain_prepare_leap_frog( dom );

    for ( int i = current_node; i < total_time_iterations; i++ ){
	domain_advance_one_time_step( dom );
	domain_write_step_to_save( dom, conf );
    }

    return;
}

void domain_prepare_leap_frog( Domain *dom )
{
    eval_charge_density( dom );
    eval_potential_and_fields( dom );    
    shift_velocities_half_time_step_back( dom );
    return;
}

void domain_advance_one_time_step( Domain *dom )
{
    push_particles( dom );
    apply_domain_constrains( dom );
    eval_charge_density( dom );
    eval_potential_and_fields( dom );
    update_time_grid( dom );
    return;
}

void eval_charge_density( Domain *dom )
{
    clear_old_density_values( dom );
    weight_particles_charge_to_mesh( dom );
    return;
}

void eval_potential_and_fields( Domain *dom )
{
    solve_poisson_eqn( dom );
    eval_fields_from_potential( dom );
    return;
}

void push_particles( Domain *dom )
{
    leap_frog( dom );
    return;
}

void apply_domain_constrains( Domain *dom )
{
    apply_domain_boundary_conditions( dom );
    //generate_new_particles();
    return;
}

//
// Eval charge density
//

void clear_old_density_values( Domain *dom )
{
    Spatial_mesh *spm = &( dom->spat_mesh );
    int nx = spm->x_n_nodes;
    int ny = spm->y_n_nodes;    
    
    for ( int i = 0; i < nx; i++ ) {
	for ( int j = 0; j < ny; j++ ) {
	    spm->charge_density[i][j] = 0;
	}
    }
}


void weight_particles_charge_to_mesh( Domain *dom )
{
    // Rewrite:
    // forall particles {
    //   find nonzero weights and corresponding nodes
    //   charge[node] = weight(particle, node) * particle.charge
    // }
    double dx = dom->spat_mesh.x_cell_size;
    double dy = dom->spat_mesh.y_cell_size;
    int tr_i, tr_j; // 'tr' = 'top_right'
    double tr_x_weight, tr_y_weight;

    for ( int i = 0; i < dom->num_of_particles; i++ ) {
	next_node_num_and_weight( vec2d_x( dom->particles[i].position ), dx, 
				  &tr_i, &tr_x_weight );
	next_node_num_and_weight( vec2d_y( dom->particles[i].position ), dy,
				  &tr_j, &tr_y_weight );
	dom->spat_mesh.charge_density[tr_i][tr_j] +=
	    tr_x_weight * tr_y_weight * dom->particles[i].charge;
	dom->spat_mesh.charge_density[tr_i-1][tr_j] +=
	    ( 1.0 - tr_x_weight ) * tr_y_weight * dom->particles[i].charge;
	dom->spat_mesh.charge_density[tr_i][tr_j-1] +=
	    tr_x_weight * ( 1.0 - tr_y_weight ) * dom->particles[i].charge;
	dom->spat_mesh.charge_density[tr_i-1][tr_j-1] +=
	    ( 1.0 - tr_x_weight ) * ( 1.0 - tr_y_weight )
	    * dom->particles[i].charge;
    }
    return;
}

void next_node_num_and_weight( const double x, const double grid_step, 
			       int *next_node, double *weight )
{
    double x_in_grid_units = x / grid_step;
    *next_node = ceil( x_in_grid_units );
    *weight = 1.0 - ( *next_node - x_in_grid_units );
    return;
}

//
// Eval potential and fields
//

void solve_poisson_eqn( Domain *dom )
{
    double a = 0.0;
    double b = dom->spat_mesh.x_volume_size;
    int nx = dom->spat_mesh.x_n_nodes;
    int M = nx-1;
    int MBDCND = 1; // 1st kind boundary conditions
    //
    double c = 0.0;
    double d = dom->spat_mesh.y_volume_size;
    int ny = dom->spat_mesh.y_n_nodes;
    int N = ny-1;
    int NBDCND = 1; // 1st kind boundary conditions
    //
    double BDA[ny]; // dummy
    double BDB[ny]; // dummy
    double BDC[nx]; // dummy
    double BDD[nx]; // dummy
    //
    double elmbda = 0.0;
    double **f_rhs = NULL;
    double hwscrt_f[ nx * ny ];
    int idimf = nx;
    //
    int w_dim = 
	4 * ( N + 1 ) + ( 13 + (int)( log2( N + 1 ) ) ) * ( M + 1 );
    double w[w_dim];
    double pertrb;
    int ierror;

    f_rhs = poisson_init_rhs( &( dom->spat_mesh ) );
    rowmajor_to_colmajor( f_rhs, hwscrt_f, nx, ny );
    hwscrt_( 
	&a, &b, &M, &MBDCND, BDA, BDB,
	&c, &d, &N, &NBDCND, BDC, BDD,
	&elmbda, hwscrt_f, &idimf, &pertrb, &ierror, w);
    if ( ierror != 0 ){
	printf( "Error while solving Poisson equation (HWSCRT). \n" );
	printf( "ierror = %d \n", ierror );
    	exit( EXIT_FAILURE );
    }
    colmajor_to_rowmajor( hwscrt_f, dom->spat_mesh.potential, nx, ny );
    poisson_free_rhs( f_rhs, nx, ny );
    return;
}


double **poisson_init_rhs( Spatial_mesh *spm )
{
    int nx = spm->x_n_nodes;
    int ny = spm->y_n_nodes;    

    double **rhs = NULL;
    rhs = (double **) malloc( nx * sizeof(double *) );
    if ( rhs == NULL ) {
	printf( "f_rhs allocate: nx: out of memory ");
	exit( EXIT_FAILURE );	
    }
    for( int i = 0; i < nx; i++) {
	rhs[i] = (double *) malloc( ny * sizeof(double) );
	if ( rhs[i] == NULL ) {
	    printf( "f_rhs allocate: ny: out of memory ");
	    exit( EXIT_FAILURE );	
	}
    }
    
    for ( int i = 1; i < nx-1; i++ ) {
	for ( int j = 1; j < ny-1; j++ ) {
	    rhs[i][j] = -4.0 * M_PI * spm->charge_density[i][j];
	}
    }

    for ( int i = 0; i < nx; i++ ) {
	rhs[i][0] = spm->potential[i][0];
	rhs[i][ny-1] = spm->potential[i][ny-1];
    }

    for ( int j = 0; j < ny; j++ ) {
	rhs[0][j] = spm->potential[0][j];
	rhs[nx-1][j] = spm->potential[nx-1][j];
    }
    
    return rhs;
}

void poisson_free_rhs( double **rhs, int nx, int ny )
{
    for( int i = 0; i < nx; i++) {
	free( rhs[i] );
    }
    free( rhs );
}

void rowmajor_to_colmajor( double **c, double *fortran, int dim1, int dim2 )
{
    for ( int j = 0; j < dim2; j++ ) {
	for ( int i = 0; i < dim1; i++ ) {
	    *( fortran + i + ( j * dim1 ) ) = c[i][j];
	}
    }
    return;
}

void colmajor_to_rowmajor( double *fortran, double **c, int dim1, int dim2 )
{
    for ( int j = 0; j < dim2; j++ ) {
	for ( int i = 0; i < dim1; i++ ) {
	    c[i][j] = *( fortran + i + ( j * dim1 ) );
	}
    }
    return;
}

void eval_fields_from_potential( Domain *dom )
{
    int nx = dom->spat_mesh.x_n_nodes;
    int ny = dom->spat_mesh.y_n_nodes;
    double dx = dom->spat_mesh.x_cell_size;
    double dy = dom->spat_mesh.y_cell_size;
    double **phi = dom->spat_mesh.potential;
    double ex[nx][ny], ey[nx][ny];

    for ( int j = 0; j < ny; j++ ) {
	for ( int i = 0; i < nx; i++ ) {
	    if ( i == 0 ) {
		ex[i][j] = - boundary_difference( phi[i][j], phi[i+1][j], dx );
	    } else if ( i == nx-1 ) {
		ex[i][j] = - boundary_difference( phi[i-1][j], phi[i][j], dx );
	    } else {
		ex[i][j] = - central_difference( phi[i-1][j], phi[i+1][j], dx );
	    }
	}
    }

    for ( int i = 0; i < nx; i++ ) {
	for ( int j = 0; j < ny; j++ ) {
	    if ( j == 0 ) {
		ey[i][j] = - boundary_difference( phi[i][j], phi[i][j+1], dy );
	    } else if ( j == ny-1 ) {
		ey[i][j] = - boundary_difference( phi[i][j-1], phi[i][j], dy );
	    } else {
		ey[i][j] = - central_difference( phi[i][j-1], phi[i][j+1], dy );
	    }
	}
    }

    for ( int i = 0; i < nx; i++ ) {
	for ( int j = 0; j < ny; j++ ) {
	    dom->spat_mesh.electric_field[i][j] = vec2d_init( ex[i][j], ey[i][j] );
	}
    }

    return;
}

double central_difference( double phi1, double phi2, double dx )
{    
    return ( (phi2 - phi1) / ( 2.0 * dx ) );
}

double boundary_difference( double phi1, double phi2, double dx )
{    
    return ( (phi2 - phi1) / dx );
}

//
// Push particles
//

void leap_frog( Domain *dom )
{  
    double dt = dom->time_grid.time_step_size;

    update_momentum( dom, dt );
    update_position( dom, dt );
    return;
}

void shift_velocities_half_time_step_back( Domain *dom )
{
    double half_dt = dom->time_grid.time_step_size / 2;

    update_momentum( dom, -half_dt );
    return;    
}

void update_momentum( Domain *dom, double dt )
{
    // Rewrite:
    // forall particles {
    //   find nonzero weights and corresponding nodes
    //   force[particle] = weight(particle, node) * force(node)
    // }
    Vec2d force, dp;

    for ( int i = 0; i < dom->num_of_particles; i++ ) {
	force = force_on_particle( dom, i );
	dp = vec2d_times_scalar( force, dt );
	dom->particles[i].momentum = vec2d_add( dom->particles[i].momentum, dp );
    }
    return;
}

Vec2d force_on_particle( Domain *dom, int particle_number )
{
    double dx = dom->spat_mesh.x_cell_size;
    double dy = dom->spat_mesh.y_cell_size;
    int tr_i, tr_j; // 'tr' = 'top_right'
    double tr_x_weight, tr_y_weight;  
    Vec2d field_from_node, total_field, force;
    //
    next_node_num_and_weight( vec2d_x( dom->particles[particle_number].position ), dx,
			      &tr_i, &tr_x_weight );
    next_node_num_and_weight( vec2d_y( dom->particles[particle_number].position ), dy,
			      &tr_j, &tr_y_weight );
    //
    total_field = vec2d_zero();
    field_from_node = vec2d_times_scalar(			
	dom->spat_mesh.electric_field[tr_i][tr_j],		
	tr_x_weight );
    field_from_node = vec2d_times_scalar( field_from_node, tr_y_weight );
    total_field = vec2d_add( total_field, field_from_node );
    //
    field_from_node = vec2d_times_scalar(		        
	dom->spat_mesh.electric_field[tr_i-1][tr_j],		
	1.0 - tr_x_weight );
    field_from_node = vec2d_times_scalar( field_from_node, tr_y_weight );
    total_field = vec2d_add( total_field, field_from_node );
    //
    field_from_node = vec2d_times_scalar(			
	dom->spat_mesh.electric_field[tr_i][tr_j - 1],	
	tr_x_weight );
    field_from_node = vec2d_times_scalar( field_from_node, 1.0 - tr_y_weight );
    total_field = vec2d_add( total_field, field_from_node );
    //
    field_from_node = vec2d_times_scalar(			
	dom->spat_mesh.electric_field[tr_i-1][tr_j-1],	
	1.0 - tr_x_weight );
    field_from_node = vec2d_times_scalar( field_from_node, 1.0 - tr_y_weight );
    total_field = vec2d_add( total_field, field_from_node );
    //
    force = vec2d_times_scalar( total_field, dom->particles[particle_number].charge );
    return force;
}

void update_position( Domain *dom, double dt )
{
    Vec2d pos_shift;

    for ( int i = 0; i < dom->num_of_particles; i++ ) {
	pos_shift = vec2d_times_scalar( dom->particles[i].momentum, dt/dom->particles[i].mass );
	dom->particles[i].position = vec2d_add( dom->particles[i].position, pos_shift );
    }
    return;
}


//
// Apply domain constrains
//

void apply_domain_boundary_conditions( Domain *dom )
{
    int i = 0;
  
    while ( i < dom->num_of_particles ) {
	if ( out_of_bound( dom, dom->particles[i].position ) ) {
	    remove_particle( &i, dom );
	} else {
	    proceed_to_next_particle( &i, dom );
	}
    }  
    return;
}

bool out_of_bound( Domain *dom, Vec2d r )
{
    double x = vec2d_x( r );
    double y = vec2d_y( r );
    bool out;
    
    out = 
	( x >= dom->spat_mesh.x_volume_size ) || ( x <= 0 ) ||
	( y >= dom->spat_mesh.y_volume_size ) || ( y <= 0 ) ;
    return out;
}

void remove_particle( int *i, Domain *dom )
{
    dom->particles[ *i ] = dom->particles[ dom->num_of_particles - 1 ];
    dom->num_of_particles--;
    return;
}

void proceed_to_next_particle( int *i, Domain *dom )
{
    (*i)++;	    
    return;
}

//
// Update time grid
//

void update_time_grid( Domain *dom )
{
    dom->time_grid.current_node++;
    dom->time_grid.current_time += dom->time_grid.time_step_size;
    return;
}


//
// Write domain to file
//

void domain_write_step_to_save( Domain *dom, Config *conf )
{
    int current_step = dom->time_grid.current_node;
    int step_to_save = dom->time_grid.node_to_save;
    if ( ( current_step % step_to_save ) == 0 ){	
	domain_write( dom, conf );
    }
    return;
}

void domain_write( Domain *dom, Config *conf )
{
    char *output_filename_prefix = conf->output_filename_prefix;
    char *output_filename_suffix = conf->output_filename_suffix;
    char *file_name_to_write;
    
    file_name_to_write = construct_output_filename( output_filename_prefix, 
						    dom->time_grid.current_node,
						    output_filename_suffix  );
			           
    FILE *f = fopen(file_name_to_write, "w");
    if (f == NULL) {
	printf( "Error: can't open file \'%s\' to save results of simulation!\n", file_name_to_write );
	printf( "Recheck 'output_filename_prefix' key in config file.\n" );
	printf( "Make sure the directory you want to save to exists.\n" );
	exit( EXIT_FAILURE );
    }
    printf ("Writing step %d to file %s\n", dom->time_grid.current_node, file_name_to_write);
	    
    dom->time_grid.write_to_file( f );
    dom->spat_mesh.write_to_file( f );
    particles_write_to_file( dom->particles, dom->num_of_particles, f );

    free( file_name_to_write );
    fclose(f);
    return;
}

char *construct_output_filename( const char *output_filename_prefix, 
				 const int current_time_step,
				 const char *output_filename_suffix )
{    
    int prefix_len = strlen(output_filename_prefix);
    int suffix_len = strlen(output_filename_suffix);
    int number_len = ((CHAR_BIT * sizeof(int) - 1) / 3 + 2); // don't know how this works
    int ENOUGH = prefix_len + number_len + suffix_len;
    char *filename;
    filename = (char *) malloc( ENOUGH * sizeof(char) );
    snprintf(filename, ENOUGH, "%s%.4d%s", 
	     output_filename_prefix, current_time_step, output_filename_suffix);
    return filename;
}

//
// Free domain
//

void domain_free( Domain *dom )
{
    printf( "TODO: free domain.\n" );
    return;
}


//
// Various functions
//

void domain_print_particles( Domain *dom )
{
    for ( int i = 0; i < dom->num_of_particles; i++ ) {
    	printf( "%d: (%.2f,%.2f), (%.2f,%.2f) \n", 
		i, 
		vec2d_x(dom->particles[i].position),
		vec2d_y(dom->particles[i].position),
		vec2d_x(dom->particles[i].momentum),
		vec2d_y(dom->particles[i].momentum));
    }
    return;
}
