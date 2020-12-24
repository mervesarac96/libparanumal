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

#include "mesh.hpp"

typedef struct {
  hlong v[4]; // vertices on face
  dlong element, elementN;
  int face, rank;    // face info
  int faceN, rankN; // N for neighbor face info

}parallelFace_t;

// mesh is the local partition
void mesh_t::ParallelConnect(){

  // serial connectivity on each process
  this->Connect();

  // count # of elements to send to each rank based on
  // minimum {vertex id % size}
  int *Nsend = (int*) calloc(size, sizeof(int));
  int *Nrecv = (int*) calloc(size, sizeof(int));
  int *sendOffsets = (int*) calloc(size, sizeof(int));
  int *recvOffsets = (int*) calloc(size, sizeof(int));

  // WARNING: In some corner cases, the number of faces to send may overrun int storage
  int allNsend = 0;
  for(dlong e=0;e<Nelements;++e){
    for(int f=0;f<Nfaces;++f){
      if(EToE[e*Nfaces+f]==-1){
        // find rank of destination for sorting based on max(face vertices)%size
        hlong maxv = 0;
        for(int n=0;n<NfaceVertices;++n){
          int nid = faceVertices[f*NfaceVertices+n];
          dlong id = EToV[e*Nverts + nid];
          maxv = mymax(maxv, id);
        }
        int destRank = (int) (maxv%size);

        // increment send size for
        ++Nsend[destRank];
        ++allNsend;
      }
    }
  }

  // find send offsets
  for(int rr=1;rr<size;++rr)
    sendOffsets[rr] = sendOffsets[rr-1] + Nsend[rr-1];

  // reset counters
  for(int rr=0;rr<size;++rr)
    Nsend[rr] = 0;

  // buffer for outgoing data
  parallelFace_t *sendFaces = (parallelFace_t*) calloc(allNsend, sizeof(parallelFace_t));

  // Make the MPI_PARALLELFACE_T data type
  MPI_Datatype MPI_PARALLELFACE_T;
  MPI_Datatype dtype[7] = {MPI_HLONG, MPI_DLONG, MPI_DLONG, MPI_INT,
                            MPI_INT, MPI_INT, MPI_INT};
  int blength[7] = {4, 1, 1, 1, 1, 1, 1};
  MPI_Aint addr[7], displ[7];
  MPI_Get_address ( &(sendFaces[0]              ), addr+0);
  MPI_Get_address ( &(sendFaces[0].element      ), addr+1);
  MPI_Get_address ( &(sendFaces[0].elementN     ), addr+2);
  MPI_Get_address ( &(sendFaces[0].face         ), addr+3);
  MPI_Get_address ( &(sendFaces[0].rank         ), addr+4);
  MPI_Get_address ( &(sendFaces[0].faceN        ), addr+5);
  MPI_Get_address ( &(sendFaces[0].rankN        ), addr+6);
  displ[0] = 0;
  displ[1] = addr[1] - addr[0];
  displ[2] = addr[2] - addr[0];
  displ[3] = addr[3] - addr[0];
  displ[4] = addr[4] - addr[0];
  displ[5] = addr[5] - addr[0];
  displ[6] = addr[6] - addr[0];
  MPI_Type_create_struct (7, blength, displ, dtype, &MPI_PARALLELFACE_T);
  MPI_Type_commit (&MPI_PARALLELFACE_T);

  // pack face data
  for(dlong e=0;e<Nelements;++e){
    for(int f=0;f<Nfaces;++f){
      if(EToE[e*Nfaces+f]==-1){

        // find rank of destination for sorting based on max(face vertices)%size
        hlong maxv = 0;
        for(int n=0;n<NfaceVertices;++n){
          int nid = faceVertices[f*NfaceVertices+n];
          hlong id = EToV[e*Nverts + nid];
          maxv = mymax(maxv, id);
        }
        int destRank = (int) (maxv%size);

        // populate face to send out staged in segment of sendFaces array
        int id = sendOffsets[destRank]+Nsend[destRank];


        sendFaces[id].element = e;
        sendFaces[id].face = f;
        for(int n=0;n<NfaceVertices;++n){
          int nid = faceVertices[f*NfaceVertices+n];
          sendFaces[id].v[n] = EToV[e*Nverts + nid];
        }

        std::sort(sendFaces[id].v, sendFaces[id].v+NfaceVertices,
                  std::less<hlong>());

        sendFaces[id].rank = rank;

        sendFaces[id].elementN = -1;
        sendFaces[id].faceN = -1;
        sendFaces[id].rankN = -1;

        ++Nsend[destRank];
      }
    }
  }

  // exchange byte counts
  MPI_Alltoall(Nsend, 1, MPI_INT,
               Nrecv, 1, MPI_INT,
               comm);

  // count incoming faces
  int allNrecv = 0;
  for(int rr=0;rr<size;++rr)
    allNrecv += Nrecv[rr];

  // find offsets for recv data
  for(int rr=1;rr<size;++rr)
    recvOffsets[rr] = recvOffsets[rr-1] + Nrecv[rr-1]; // byte offsets

  // buffer for incoming face data
  parallelFace_t *recvFaces = (parallelFace_t*) calloc(allNrecv, sizeof(parallelFace_t));

  // exchange parallel faces
  MPI_Alltoallv(sendFaces, Nsend, sendOffsets, MPI_PARALLELFACE_T,
                recvFaces, Nrecv, recvOffsets, MPI_PARALLELFACE_T,
                comm);

  // local sort allNrecv received faces
  std::sort(recvFaces, recvFaces+allNrecv,
            [&](const parallelFace_t& a, const parallelFace_t& b) {
              return std::lexicographical_compare(a.v, a.v+NfaceVertices,
                                                  b.v, b.v+NfaceVertices);
            });

  // find matches
  for(int n=0;n<allNrecv-1;++n){
    // since vertices are ordered we just look for pairs
    if(std::equal(recvFaces[n].v, recvFaces[n].v+NfaceVertices,
                  recvFaces[n+1].v)){
      recvFaces[n].elementN = recvFaces[n+1].element;
      recvFaces[n].faceN = recvFaces[n+1].face;
      recvFaces[n].rankN = recvFaces[n+1].rank;

      recvFaces[n+1].elementN = recvFaces[n].element;
      recvFaces[n+1].faceN = recvFaces[n].face;
      recvFaces[n+1].rankN = recvFaces[n].rank;
    }
  }

  // sort back to original ordering
  std::sort(recvFaces, recvFaces+allNrecv,
            [](const parallelFace_t& a, const parallelFace_t& b) {
              if(a.rank < b.rank) return true;
              if(a.rank > b.rank) return false;

              if(a.element < b.element) return true;
              if(a.element > b.element) return false;

              return (a.face < b.face);
            });

  // send faces back from whence they came
  MPI_Alltoallv(recvFaces, Nrecv, recvOffsets, MPI_PARALLELFACE_T,
                sendFaces, Nsend, sendOffsets, MPI_PARALLELFACE_T,
                comm);

  // extract connectivity info
  EToP = (int*) calloc(Nelements*Nfaces, sizeof(int));
  for(dlong cnt=0;cnt<Nelements*Nfaces;++cnt)
    EToP[cnt] = -1;

  for(int cnt=0;cnt<allNsend;++cnt){
    dlong e = sendFaces[cnt].element;
    dlong eN = sendFaces[cnt].elementN;
    int f = sendFaces[cnt].face;
    int fN = sendFaces[cnt].faceN;
    int rN = sendFaces[cnt].rankN;

    if(e>=0 && f>=0 && eN>=0 && fN>=0){
      EToE[e*Nfaces+f] = eN;
      EToF[e*Nfaces+f] = fN;
      EToP[e*Nfaces+f] = rN;
    }
  }

  MPI_Barrier(comm);
  MPI_Type_free(&MPI_PARALLELFACE_T);
  free(sendFaces);
  free(recvFaces);

  //record the number of elements in the whole mesh
  hlong NelementsLocal = (hlong) Nelements;
  NelementsGlobal = 0;
  MPI_Allreduce(&NelementsLocal, &NelementsGlobal, 1, MPI_HLONG, MPI_SUM, comm);
}
