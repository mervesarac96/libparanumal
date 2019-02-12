/*

The MIT License (MIT)

Copyright (c) 2017 Tim Warburton, Noel Chalmers, Jesse Chan, Ali Karakus

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.

*/

#include "mesh3D.h"

#include <p4est_to_p8est.h>
#include <p8est.h>
#include <p8est_bits.h>
#include <p8est_connectivity.h>
#include <p8est_extended.h>
#include <p8est_ghost.h>
#include <p8est_iterate.h>
#include <p8est_lnodes.h>
#include <p8est_mesh.h>
#include <p8est_nodes.h>
#include <p8est_tets_hexes.h>
#include <p8est_vtk.h>

mesh3D *adaptiveSetupBoxHex3D(int N, setupAide &options){

  //  mesh_t *mesh = new mesh_t[1];
  mesh_t *mesh = (mesh_t*) calloc(1, sizeof(mesh_t));
  
  int rank, size;
  
  MPI_Comm_rank(MPI_COMM_WORLD, &rank);
  MPI_Comm_size(MPI_COMM_WORLD, &size);
  
  MPI_Comm_dup(MPI_COMM_WORLD, &mesh->comm);

  sc_init(mesh->comm, 0, 0, NULL, SC_LP_STATISTICS);
  p4est_init(NULL, SC_LP_STATISTICS);

  mesh->rank = rank;
  mesh->size = size;
  
  mesh->Nfields = 1;
  mesh->dim = 3;
  mesh->Nverts = 8; // number of vertices per element
  mesh->Nfaces = 6;
  mesh->NfaceVertices = 4;
  
  // vertices on each face
  int faceVertices[6][4] =
    {{0,1,2,3},{0,1,5,4},{1,2,6,5},{2,3,7,6},{3,0,4,7},{4,5,6,7}};

  mesh->faceVertices =
    (int*) calloc(mesh->NfaceVertices*mesh->Nfaces, sizeof(int));

  memcpy(mesh->faceVertices, faceVertices[0], mesh->NfaceVertices*mesh->Nfaces*sizeof(int));
  
  // build an NX x NY x NZ periodic box grid
  
  hlong NX = 10, NY = 10, NZ = 10; // defaults
  hlong level = 0;

  options.getArgs("BOX NX", NX);
  options.getArgs("BOX NY", NY);
  options.getArgs("BOX NZ", NZ);
  options.getArgs("BOX REFINE LEVEL", level);

  dfloat XMIN = -1, XMAX = +1; // default bi-unit cube
  dfloat YMIN = -1, YMAX = +1;
  dfloat ZMIN = -1, ZMAX = +1;
  
  options.getArgs("BOX XMIN", XMIN);
  options.getArgs("BOX YMIN", YMIN);
  options.getArgs("BOX ZMIN", ZMIN);

  options.getArgs("BOX XMAX", XMAX);
  options.getArgs("BOX YMAX", YMAX);
  options.getArgs("BOX ZMAX", ZMAX);


  
  hlong allNelements = NX*NY*NZ;

  hlong chunkNelements = allNelements/size;

  hlong start = chunkNelements*rank;
  hlong end   = chunkNelements*(rank+1);
  
  if(mesh->rank==(size-1))
    end = allNelements;

  mesh->Nnodes = NX*NY*NZ; // assume periodic and global number of nodes
  mesh->Nelements = end-start;
  mesh->NboundaryFaces = 0;

  printf("Rank %d initially has %d elements\n", mesh->rank, mesh->Nelements);
  
  mesh->EToV = (hlong*) calloc(mesh->Nelements*mesh->Nverts, sizeof(hlong));

  mesh->EX = (dfloat*) calloc(mesh->Nelements*mesh->Nverts, sizeof(dfloat));
  mesh->EY = (dfloat*) calloc(mesh->Nelements*mesh->Nverts, sizeof(dfloat));
  mesh->EZ = (dfloat*) calloc(mesh->Nelements*mesh->Nverts, sizeof(dfloat));

  mesh->elementInfo = (hlong*) calloc(mesh->Nelements, sizeof(hlong));
  
  // [0,NX]
  dfloat dx = (XMAX-XMIN)/NX; // xmin+0*dx, xmin + NX*(XMAX-XMIN)/NX
  dfloat dy = (YMAX-YMIN)/NY;
  dfloat dz = (ZMAX-ZMIN)/NZ;
  for(hlong n=start;n<end;++n){

    int i = n%NX;      // [0, NX)
    int j = (n/NY)%NZ; // [0, NY)
    int k = n/(NX*NY); // [0, NZ)

    hlong e = n-start;

    int ip = (i+1)%NX;
    int jp = (j+1)%NY;
    int kp = (k+1)%NZ;

    // do not use for coordinates
    // ADAPT HERE:
    mesh->EToV[e*mesh->Nverts+0] = i  +  j*NX + k*NX*NY;
    mesh->EToV[e*mesh->Nverts+1] = ip +  j*NX + k*NX*NY;
    mesh->EToV[e*mesh->Nverts+2] = ip + jp*NX + k*NX*NY;
    mesh->EToV[e*mesh->Nverts+3] = i  + jp*NX + k*NX*NY;

    mesh->EToV[e*mesh->Nverts+4] = i  +  j*NX + kp*NX*NY;
    mesh->EToV[e*mesh->Nverts+5] = ip +  j*NX + kp*NX*NY;
    mesh->EToV[e*mesh->Nverts+6] = ip + jp*NX + kp*NX*NY;
    mesh->EToV[e*mesh->Nverts+7] = i  + jp*NX + kp*NX*NY;

    dfloat xo = XMIN + dx*i;
    dfloat yo = YMIN + dy*j;
    dfloat zo = ZMIN + dz*k;
    
    dfloat *ex = mesh->EX+e*mesh->Nverts;
    dfloat *ey = mesh->EY+e*mesh->Nverts;
    dfloat *ez = mesh->EZ+e*mesh->Nverts;

    // ADAPT HERE:
    ex[0] = xo;    ey[0] = yo;    ez[0] = zo;
    ex[1] = xo+dx; ey[1] = yo;    ez[1] = zo;
    ex[2] = xo+dx; ey[2] = yo+dy; ez[2] = zo;
    ex[3] = xo;    ey[3] = yo+dy; ez[3] = zo;

    ex[4] = xo;    ey[4] = yo;    ez[4] = zo+dz;
    ex[5] = xo+dx; ey[5] = yo;    ez[5] = zo+dz;
    ex[6] = xo+dx; ey[6] = yo+dy; ez[6] = zo+dz;
    ex[7] = xo;    ey[7] = yo+dy; ez[7] = zo+dz;

    // ADAPT HERE: hard coded
    mesh->elementInfo[e] = 1; // ?
    
  }
  {
    p8est_connectivity_t *conn = p8est_connectivity_new_brick(NX, NY, NZ, 1, 1, 1);
    p8est_t *pxest = p8est_new_ext(mesh->comm, conn, 0, level, 1, 0,
        NULL, NULL);
    p8est_balance_ext(pxest, P4EST_CONNECT_FULL, NULL, NULL);
    p8est_partition(pxest, 1, NULL);
    p8est_ghost_t *ghost = p8est_ghost_new(pxest, P4EST_CONNECT_FULL);

    p8est_lnodes_t *lnodes = p8est_lnodes_new(pxest, ghost, 1);
    p8est_ghost_support_lnodes(pxest, lnodes, ghost);
    p8est_ghost_expand_by_lnodes(pxest, lnodes, ghost);

    p8est_lnodes_destroy(lnodes);
    p8est_ghost_destroy(ghost);
    p8est_destroy(pxest);
    p8est_connectivity_destroy(conn);
  }
  
  // partition elements using Morton ordering & parallel sort
  meshGeometricPartition3D(mesh);

  mesh->EToB = (int*) calloc(mesh->Nelements*mesh->Nfaces, sizeof(int)); 

  mesh->boundaryInfo = NULL; // no boundaries
  
  // connect elements using parallel sort
  meshParallelConnect(mesh); // => mesh->EToE, mesh->EToF, mesh->EToP
  
  // print out connectivity statistics
  meshPartitionStatistics(mesh);

  // load reference (r,s,t) element nodes
  meshLoadReferenceNodesHex3D(mesh, N);

  // compute physical (x,y) locations of the element nodes
  meshPhysicalNodesHex3D(mesh);

  // compute geometric factors
  meshGeometricFactorsHex3D(mesh);

  // set up halo exchange info for MPI (do before connect face nodes)
  // ADAPT: noncon will break
  meshHaloSetup(mesh);

  // connect face nodes (find trace indices)
  // ADAPT: noncon will break
  meshConnectPeriodicFaceNodes3D(mesh,XMAX-XMIN,YMAX-YMIN,ZMAX-ZMIN); // needs to fix this !

  // connect elements to boundary faces
  //  meshConnectBoundary(mesh);
  
  // compute surface geofacs (including halo)
  // ADAPT: noncon will break
  meshSurfaceGeometricFactorsHex3D(mesh);
  
  // global nodes
  // ADAPT: noncon will break
  meshParallelConnectNodes(mesh); 

  // initialize LSERK4 time stepping coefficients
  int Nrk = 5;

  dfloat rka[5] = {0.0,
		   -567301805773.0/1357537059087.0 ,
		   -2404267990393.0/2016746695238.0 ,
		   -3550918686646.0/2091501179385.0  ,
		   -1275806237668.0/842570457699.0};
  dfloat rkb[5] = { 1432997174477.0/9575080441755.0 ,
		    5161836677717.0/13612068292357.0 ,
		    1720146321549.0/2090206949498.0  ,
		    3134564353537.0/4481467310338.0  ,
		    2277821191437.0/14882151754819.0};
  dfloat rkc[6] = {0.0  ,
		   1432997174477.0/9575080441755.0 ,
		   2526269341429.0/6820363962896.0 ,
		   2006345519317.0/3224310063776.0 ,
		   2802321613138.0/2924317926251.0,
		   1.}; 

  mesh->Nrk = Nrk;
  memcpy(mesh->rka, rka, Nrk*sizeof(dfloat));
  memcpy(mesh->rkb, rkb, Nrk*sizeof(dfloat));
  memcpy(mesh->rkc, rkc, (Nrk+1)*sizeof(dfloat));
 
  return mesh;
}