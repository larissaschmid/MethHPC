#include "block_matmul.h"

struct Config {
	/* MPI Files */
	MPI_File A_file, B_file, C_file;
	char *outfile;

	/* MPI Datatypes for matrix blocks */
	MPI_Datatype block;

	/* Matrix data */
	double *A, *A_tmp, *B, *C;

	/* Cart communicators */
	MPI_Comm grid_comm;
	MPI_Comm row_comm;
	MPI_Comm col_comm;

	/* Cart communicator dim and ranks */
	int dim[2], coords[2];
	int world_rank, world_size, grid_rank;
	int row_rank, row_size, col_rank, col_size;

	/* Full matrix dim */
	int A_dims[2];
	int B_dims[2];
	int C_dims[2];
	int matrix_size;

	/* Process local matrix dim */
	int local_dims[2];
	int local_size;
};

struct Config config;

void init_matmul(char *A_file, char *B_file, char *outfile)
{
	MPI_Comm_size(MPI_COMM_WORLD, &config.world_size);
	MPI_Comm_rank(MPI_COMM_WORLD, &config.world_rank);

	/* Copy output file name to configuration */
	config.outfile = outfile;
	/* Get matrix size header */
	
	if(config.world_rank == 0) {
		MPI_File_open(MPI_COMM_SELF, A_file, MPI_MODE_RDONLY, MPI_INFO_NULL, &config.A_file);
		MPI_File_open(MPI_COMM_SELF, B_file, MPI_MODE_RDONLY, MPI_INFO_NULL, &config.B_file);

		MPI_File_read(config.A_file, &config.A_dims, 2, MPI_INT, MPI_STATUS_IGNORE);
		MPI_File_read(config.B_file, &config.B_dims, 2, MPI_INT, MPI_STATUS_IGNORE);
		
		MPI_File_close(&config.A_file);
		MPI_File_close(&config.B_file);
	}

	MPI_Bcast(config.A_dims, 2, MPI_INT, 0, MPI_COMM_WORLD);
	MPI_Bcast(config.B_dims, 2, MPI_INT, 0, MPI_COMM_WORLD);

	config.C_dims[0] = config.A_dims[0];
	config.C_dims[1] = config.B_dims[1];
	config.matrix_size = config.C_dims[0] * config.C_dims[1];

	/* Set dim of tiles relative to the number of processes as NxN where N=sqrt(world_size) */
	int N = sqrt(config.world_size);
	config.dim[0] = N;
	config.dim[1] = N;
	

	/* Verify dim of A and B matches for matul and both are square*/
	if((config.A_dims[0] != config.A_dims[1]) | (config.A_dims[1] != config.B_dims[0]) | (config.B_dims[0] != config.B_dims[1])) {
		printf("Dimension mismatch");
	}

	/* Create Cart communicator for NxN processes */
	int periods[2] = {1, 0};
	MPI_Cart_create(MPI_COMM_WORLD, 2, config.dim, periods, 0, &config.grid_comm);
	MPI_Comm_rank(config.grid_comm, &config.grid_rank);
	MPI_Cart_coords(config.grid_comm, config.grid_rank, 2, config.coords);
	
	/* Sub div cart communicator to N row communicator */
	int remain_row[2] = {0, 1};
	MPI_Cart_sub(config.grid_comm, remain_row, &config.row_comm);
	MPI_Comm_rank(config.row_comm, &config.row_rank);
	MPI_Comm_size(config.row_comm, &config.row_size);

	/* Sub div cart communicator to N col communicator */
	int remain_column[2] = {1, 0};
	MPI_Cart_sub(config.grid_comm, remain_column, &config.col_comm);
	MPI_Comm_rank(config.col_comm, &config.col_rank);
	MPI_Comm_size(config.col_comm, &config.col_size);

	/* Setup sizes of full matrices */


	/* Setup sizes of local matrix tiles */
	config.local_dims[0] = config.A_dims[0] / config.dim[0];
	config.local_dims[1] = config.A_dims[1] / config.dim[1];
	config.local_size = config.local_dims[0] * config.local_dims[1];

	/* Create subarray datatype for local matrix tile */
	int corner[2] = {config.local_dims[0]*config.coords[0], config.local_dims[1]*config.coords[1]};
	MPI_Type_create_subarray(2, config.A_dims, config.local_dims, corner, MPI_ORDER_C, MPI_DOUBLE, &config.block);
	MPI_Type_commit(&config.block);

	/* Create data array to load actual block matrix data */
	double * A = (double *) malloc(config.local_size * sizeof(double));
	config.A = A;
	config.A_tmp = (double*) calloc(sizeof(double), config.local_size);
	config.B = (double*) calloc(sizeof(double), config.local_size);
	config.C = (double*) calloc(sizeof(double), config.local_size);
	
	/* Set fileview of process to respective matrix block */
	
	MPI_Offset offset = 2 * sizeof(int);
	
	MPI_File_open(MPI_COMM_WORLD, A_file, MPI_MODE_RDONLY, MPI_INFO_NULL, &config.A_file);
	MPI_File_open(MPI_COMM_WORLD, B_file, MPI_MODE_RDONLY, MPI_INFO_NULL, &config.B_file);

	MPI_File_set_view(config.A_file, offset, MPI_DOUBLE, config.block, "native", MPI_INFO_NULL);
	MPI_File_set_view(config.B_file, offset, MPI_DOUBLE, config.block, "native", MPI_INFO_NULL);
	MPI_File_read_all(config.A_file, config.A, config.local_size, MPI_DOUBLE, MPI_STATUS_IGNORE);
	MPI_File_read_all(config.B_file, config.B, config.local_size, MPI_DOUBLE, MPI_STATUS_IGNORE);
	

   		
  	
	/* Close data source files */
	MPI_File_close(&config.A_file);
	MPI_File_close(&config.B_file);
}

void cleanup_matmul()
{
	MPI_File_open(MPI_COMM_WORLD, config.outfile, MPI_MODE_WRONLY | MPI_MODE_CREATE, MPI_INFO_NULL, &config.C_file);

	/* Rank zero writes header specifying dim of result matrix C */
	if(config.world_rank == 0) {
		MPI_File_write(config.C_file, &config.A_dims, 2, MPI_INT, MPI_STATUS_IGNORE);
	}

	/* Set fileview of process to respective matrix block with header offset */
	MPI_File_set_view(config.C_file, sizeof(int) * 2, MPI_DOUBLE, config.block, "native", MPI_INFO_NULL);	
	
	/* Collective write and close file */
	MPI_File_write_all(config.C_file, config.C, config.local_size, MPI_DOUBLE, MPI_STATUS_IGNORE);
	//MPI_File_write_all(config.C_file, config.C, 1, config.block, MPI_STATUS_IGNORE);
	MPI_File_close(&config.C_file);

	/* Cleanup */
	free(config.A);
	free(config.A_tmp);
	free(config.B);
	free(config.C);
}

void compute_fox()
{
/* Compute source and target for verticle shift of B blocks */

	memcpy( config.A_tmp, config.A, sizeof(double)*config.local_size );

        int source_proc = (config.col_rank+1)%config.col_size;
        int target_proc = ((config.col_rank-1)+config.col_size)%config.col_size;

	int i;
	for ( i = 0; i < config.dim[0]; i++) {
		/* Diag + i broadcast block A horizontally and use A_tmp to preserve own local A*/
	
		MPI_Bcast(config.A, config.local_size, MPI_DOUBLE, (i+config.col_rank)%config.row_size, config.row_comm);

		/* dgemm with blocks */
		cblas_dgemm(CblasRowMajor,CblasNoTrans,	CblasNoTrans, config.local_dims[0], config.local_dims[1], config.local_dims[1], 1.0, config.A, config.local_dims[0], config.B, config.local_dims[0], 1.0, config.C, config.local_dims[0]);

		/* Shfting block B upwards and receive from process below */
		MPI_Sendrecv_replace(config.B, config.local_size, MPI_DOUBLE, target_proc, 0, source_proc, 0, config.col_comm, MPI_STATUS_IGNORE );
		memcpy(config.A, config.A_tmp, config.local_size*sizeof(double) );
}

}
