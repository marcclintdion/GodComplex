#include "../../GodComplex.h"

const float	SHProbeEncoder::Z_INFINITY = 1e6f;
const float	SHProbeEncoder::Z_INFINITY_TEST = 0.99f * SHProbeEncoder::Z_INFINITY;

float		SHProbeEncoder::DISTANCE_THRESHOLD = 0.02f;						// 2cm
float		SHProbeEncoder::ANGULAR_THRESHOLD = acosf( 0.5f * PI / 180 );	// 0.5�
float		SHProbeEncoder::ALBEDO_HUE_THRESHOLD = 0.04f;					// Close colors!
float		SHProbeEncoder::ALBEDO_RGB_THRESHOLD = 0.16f;					// Close colors!

const double	SHProbeEncoder::SAMPLE_SH_NORMALIZER = FOURPI / SHProbeEncoder::MAX_PROBE_SAMPLES;

SHProbeEncoder::SHProbeEncoder() {

	//////////////////////////////////////////////////////////////////////////
	// Prepare the cube map face transforms
	// Here are the transform to render the 6 faces of a cube map
	// Remember the +Z face is not oriented the same way as our Z vector: http://msdn.microsoft.com/en-us/library/windows/desktop/bb204881(v=vs.85).aspx
	//
	//
	//		^ +Y
	//		|   +Z  (our actual +Z faces the other way!)
	//		|  /
	//		| /
	//		|/
	//		o------> +X
	//
	//
	float3	SideAt[6] = {
		float3(  1,  0,  0 ),
		float3( -1,  0,  0 ),
		float3(  0,  1,  0 ),
		float3(  0, -1,  0 ),
		float3(  0,  0,  1 ),
		float3(  0,  0, -1 ),
	};
	float3	SideRight[6] = {
		float3(  0, 0, -1 ),
		float3(  0, 0,  1 ),
		float3(  1, 0,  0 ),
		float3(  1, 0,  0 ),
		float3(  1, 0,  0 ),
		float3( -1, 0,  0 ),
	};

	for ( int CubeFaceIndex=0; CubeFaceIndex < 6; CubeFaceIndex++ )
	{
		float4x4	Camera2Local;
		Camera2Local.SetRow( 0, SideRight[CubeFaceIndex], 0 );
		Camera2Local.SetRow( 1, SideAt[CubeFaceIndex].Cross( SideRight[CubeFaceIndex] ), 0 );
		Camera2Local.SetRow( 2, SideAt[CubeFaceIndex], 0 );
		Camera2Local.SetRow( 3, float3::Zero, 1 );

		m_Side2World[CubeFaceIndex] = Camera2Local;	// We don't care about the local=>world transform of the probe, we assume it's the identity matrix
	}

	//////////////////////////////////////////////////////////////////////////
	// Prepare cube map pixels
	m_pCubeMapPixels = new Pixel[6*CUBE_MAP_SIZE*CUBE_MAP_SIZE];

	double	dA = 4.0 / (CUBE_MAP_SIZE*CUBE_MAP_SIZE);	// Cube face is supposed to be in [-1,+1], yielding a 2x2 square units
	double	SumSolidAngle = 0.0;

	Pixel*	pPixel = m_pCubeMapPixels;
	for ( int CubeFaceIndex=0; CubeFaceIndex < 6; CubeFaceIndex++ )
		for ( int Y=0; Y < CUBE_MAP_SIZE; Y++ )
			for ( int X=0; X < CUBE_MAP_SIZE; X++, pPixel++ ) {
				pPixel->Index = pPixel - m_pCubeMapPixels;
				pPixel->CubeFaceIndex = CubeFaceIndex;
				pPixel->CubeFaceX = X;
				pPixel->CubeFaceY = Y;

				// Build world-space view vector
				float3	csView( 2.0f * (0.5f + X) / CUBE_MAP_SIZE - 1.0f, 1.0f - 2.0f * (0.5f + Y) / CUBE_MAP_SIZE, 1.0f );
				float	Distance2Texel = csView.Length();
						csView = csView / Distance2Texel;
				float3	wsView = float4( csView, 0 ) * m_Side2World[CubeFaceIndex];
				pPixel->View = wsView;

				// Retrieve the cube map texel's solid angle (from http://people.cs.kuleuven.be/~philip.dutre/GI/TotalCompendium.pdf)
				// dw = cos(Theta).dA / r�
				// cos(Theta) = Adjacent/Hypothenuse = 1/r
				//
				double	SolidAngle = dA / (Distance2Texel * Distance2Texel * Distance2Texel);
				SumSolidAngle += SolidAngle;

				pPixel->SolidAngle = SolidAngle;

				// Build SH coefficients
				pPixel->InitSH();
			}

	//////////////////////////////////////////////////////////////////////////
	// Prepare equal subdivisions of the sphere using Hemmersley sampling of the sphere and grouping
	for ( int SampleIndex=0; SampleIndex < MAX_PROBE_SAMPLES; SampleIndex++ ) {
		Sample&	S = m_pSamples[SampleIndex];

		S.Index = SampleIndex;
		S.OriginalPixelsCount = 0;

		// Build the sample's direction
		float	Phi = 2.0f * PI * (SampleIndex+0.5f) / MAX_PROBE_SAMPLES;
		float	Y = 2.0f * ReverseBits( 1+SampleIndex ) - 1.0f;
		float	Theta = acosf( Y );
		S.View.Set( sinf(Phi)*sinf(Theta), cosf(Theta), cosf(Phi)*sinf(Theta) );
	}

	// Assign their sample to each pixel
	int	PixelsCount = 6*CUBE_MAP_FACE_SIZE;
		pPixel = m_pCubeMapPixels;
	m_MaxSamplePixelsCount = 0;
	for ( int i=0; i < PixelsCount; i++, pPixel++ ) {

		Sample*	pSample = m_pSamples;
		float	BestSampleWeight = 0.0f;
		for ( int SampleIndex=0; SampleIndex < MAX_PROBE_SAMPLES; SampleIndex++, pSample++ ) {
			float	SampleWeight = pSample->View.Dot( pPixel->View );
			if ( SampleWeight <= BestSampleWeight ) {
				continue;
			}

			pPixel->pParentSample = pSample;	// Found a better sample for the pixel!
			BestSampleWeight = SampleWeight;
		}

		pSample = pPixel->pParentSample;
		if ( pSample->pCenterPixel == NULL || pSample->pCenterPixel->Importance < BestSampleWeight ) {
			pSample->pCenterPixel = pPixel;		// Found a better center pixel for the sample!
		}
		pPixel->Importance = BestSampleWeight;	// Assign the sample's weight as temporary importance for the pixel so the sample can compare and choose the best center pixel

		pSample->OriginalPixelsCount++;
		m_MaxSamplePixelsCount = max( m_MaxSamplePixelsCount, pSample->OriginalPixelsCount );	// Keep track of the maximum amount of pixels encountered across all samples
	}

	// Pre-allocate the maximum amount of radix nodes
	Pixel::ms_RadixNodes[0] = new SHProbeEncoder::Pixel::RadixNode_t[6*CUBE_MAP_FACE_SIZE];
	Pixel::ms_RadixNodes[1] = new SHProbeEncoder::Pixel::RadixNode_t[6*CUBE_MAP_FACE_SIZE];
}

SHProbeEncoder::~SHProbeEncoder() {
	SAFE_DELETE_ARRAY( Pixel::ms_RadixNodes[1] );
	SAFE_DELETE_ARRAY( Pixel::ms_RadixNodes[0] );
	SAFE_DELETE_ARRAY( m_pCubeMapPixels );
}

void	SHProbeEncoder::EncodeProbeCubeMap( Texture2D& _StagingCubeMap, U32 _ProbeID, U32 _ProbesCount ) {
	int	TotalPixelsCount = 6*CUBE_MAP_FACE_SIZE;

	//////////////////////////////////////////////////////////////////////////
	// 1] Read back probe data and prepare pixels for encoding
	ReadBackProbeCubeMap( _StagingCubeMap );


	//////////////////////////////////////////////////////////////////////////
	// 2] Encode static lighting & directional occlusion
	//

	// 2.1) ======== Build occlusion & static lighting SH ========
	double	SHR[9] = { 0.0 };
	double	SHG[9] = { 0.0 };
	double	SHB[9] = { 0.0 };
	double	SHOcclusion[9] = { 0.0 };

	for ( int PixelIndex=0; PixelIndex < TotalPixelsCount; PixelIndex++ ) {
		Pixel&	P = m_pCubeMapPixels[PixelIndex];
		for ( int i=0; i < 9; i++ ) {
			// Accumulate smoothed out static lighting
			SHR[i] += P.SHCoeffs[i] * P.SolidAngle * P.SmoothedStaticLitColor.x;
			SHG[i] += P.SHCoeffs[i] * P.SolidAngle * P.SmoothedStaticLitColor.y;
			SHB[i] += P.SHCoeffs[i] * P.SolidAngle * P.SmoothedStaticLitColor.z;

			// No obstacle means direct lighting from the ambient sky...
			// Accumulate SH coefficients in that direction, weighted by the solid angle
			SHOcclusion[i] += P.SolidAngle * P.SmoothedInfinity * P.SHCoeffs[i];
		}
	}

	for ( int i=0; i < 9; i++ ) {
		m_StaticSH[i].Set( (float) SHR[i], (float) SHG[i], (float) SHB[i] );
		m_OcclusionSH[i] = (float) SHOcclusion[i];
	}

	// 2.2) ======== Apply filtering ========
// 	SphericalHarmonics.SHFunctions.FilterLanczos( m_StaticSH, 3 );		// Lanczos should be okay for static lighting
// 	SphericalHarmonics.SHFunctions.FilterHanning( m_OcclusionSH, 3 );


// 	// 2.3) ======== Compute the influence of the probe on each scene face ========
// 	m_MaxFaceIndex = 0;
// 	m_ProbeInfluencePerFace.Clear();
// 	for ( int PixelIndex=0; PixelIndex < TotalPixelsCount; PixelIndex++ ) {
// 		Pixel&	P = m_pCubeMapPixels[PixelIndex];
// 		if ( P.Infinity )
// 			continue;
// 
// 		double*	pFaceInfluence = m_ProbeInfluencePerFace.Get( P.FaceIndex );
// 		if ( pFaceInfluence == NULL ) {
// 			pFaceInfluence = &m_ProbeInfluencePerFace.Add( P.FaceIndex, 0.0 );
// 		}
// 
// 		*pFaceInfluence += P.SolidAngle;
// 		m_MaxFaceIndex = max( m_MaxFaceIndex, P.FaceIndex );
// 	}


	//////////////////////////////////////////////////////////////////////////
	// 3] Build the neighbor probes network
	//
	m_NeighborProbes.Init( _ProbesCount );
	m_NeighborProbes.Clear();

	Dictionary<NeighborProbe*>	NeighborProbeID2Probe;
	for ( int PixelIndex=0; PixelIndex < TotalPixelsCount; PixelIndex++ ) {
		Pixel&	P = m_pCubeMapPixels[PixelIndex];
		if ( P.NeighborProbeID == ~0UL ) {
			continue;
		}

		NeighborProbe**	NP = NeighborProbeID2Probe.Get( P.NeighborProbeID );
		if ( NP == NULL ) {
			NeighborProbe&	Temp = m_NeighborProbes.Append();
			Temp.ProbeID = P.NeighborProbeID;
			NP = &NeighborProbeID2Probe.Add( P.NeighborProbeID, &Temp );
		}

		// Accumulate direction, solid angle & distance
		(*NP)->SolidAngle += P.SolidAngle;
		(*NP)->Distance += P.NeighborProbeDistance;
		(*NP)->Direction = (*NP)->Direction + P.View;

		// Accumulate SH for neighbor's exchange of energy
		for ( int i=0; i < 9; i++ ) {
			(*NP)->SH[i] += P.SolidAngle * P.SHCoeffs[i];
		}

		(*NP)->PixelsCount++;
	}

	// Normalize everything
	m_NearestNeighborProbeDistance = FLT_MAX;
	m_FarthestNeighborProbeDistance = 0.0f;
	for ( int NeighborIndex=0; NeighborIndex < m_NeighborProbes.GetCount(); NeighborIndex++ ) {
		NeighborProbe&	NP = m_NeighborProbes[NeighborIndex];

		NP.Distance /= NP.PixelsCount;
		NP.Direction.Normalize();

		m_NearestNeighborProbeDistance = min( m_NearestNeighborProbeDistance, NP.Distance );
		m_FarthestNeighborProbeDistance = max( m_FarthestNeighborProbeDistance, NP.Distance );
	}

	// Sort from most important to least important neighbor
	{
		class	Comparer : public IComparer<NeighborProbe> {
		public:	virtual int		Compare( const NeighborProbe& a, const NeighborProbe& b ) const {
				if ( a.SolidAngle < b.SolidAngle )
					return +1;
				if ( a.SolidAngle > b.SolidAngle )
					return -1;

				return 0;
			}
		} Comp;
	 	m_NeighborProbes.Sort( Comp );
	}

	//////////////////////////////////////////////////////////////////////////
	// 4] Build samples by flood filling
	//
	ComputeFloodFill( 1.0f, 1.0f, 1.0f, 0.5f );
}

FILE*	g_pFile = NULL;
template< typename T> void	Write( const T& _value ) {
	fwrite( &_value, sizeof(T), 1, g_pFile );
}

void	SHProbeEncoder::Save( const char* _FileName ) const {

	fopen_s( &g_pFile, _FileName, "wb" );
	ASSERT( g_pFile != NULL, "Locked!" );

	// Write the mean, harmonic mean, min, max distances
	Write( (float) m_MeanDistance );
	Write( (float) m_MeanHarmonicDistance );
	Write( (float) m_MinDistance );
	Write( (float) m_MaxDistance );

	// Write the BBox
	Write( m_BBoxMin.x );
	Write( m_BBoxMin.y );
	Write( m_BBoxMin.z );
	Write( m_BBoxMax.x );
	Write( m_BBoxMax.y );
	Write( m_BBoxMax.z );

	// Write static SH
	for ( int i=0; i < 9; i++ )
	{
		Write( m_StaticSH[i].x );
		Write( m_StaticSH[i].y );
		Write( m_StaticSH[i].z );
	}

	// Write occlusion SH
	for ( int i=0; i < 9; i++ )
		Write( m_OcclusionSH[i] );

	// Write the result samples
	Write( m_ValidSamplesCount );
	for ( U32 i=0; i < m_ValidSamplesCount; i++ ) {
		const Sample&	S = *m_ppValidSamples[i];

		// Write position, normal, albedo
		Write( S.Position.x );
		Write( S.Position.y );
		Write( S.Position.z );

		Write( S.Normal.x );
		Write( S.Normal.y );
		Write( S.Normal.z );

		Write( S.Tangent.x );
		Write( S.Tangent.y );
		Write( S.Tangent.z );

		Write( S.BiTangent.x );
		Write( S.BiTangent.y );
		Write( S.BiTangent.z );

		Write( S.Radius );

		Write( (float) (S.Albedo.x * INVPI) );
		Write( (float) (S.Albedo.y * INVPI) );
		Write( (float) (S.Albedo.z * INVPI) );

		Write( S.F0.x );
		Write( S.F0.y );
		Write( S.F0.z );

		// Write SH coefficients
		for ( int i=0; i < 9; i++ ) {
			Write( S.SH[i] );
		}
	}

 	Write( U32(0) );
// 	// Write the emissive surfaces
// 	Write( m_EmissiveSurfacesCount );
// 
// 	for ( U32 i=0; i < m_EmissiveSurfacesCount; i++ ) {
// 		Surface&	P = *m_ppEmissiveSurfaces[i];
// 
// 		// Write position, normal, albedo
// 		Write( P.Position.x );
// 		Write( P.Position.y );
// 		Write( P.Position.z );
// 
// 		Write( P.Normal.x );
// 		Write( P.Normal.y );
// 		Write( P.Normal.z );
// 
// 		Write( P.Tangent.x );
// 		Write( P.Tangent.y );
// 		Write( P.Tangent.z );
// 
// 		Write( P.BiTangent.x );
// 		Write( P.BiTangent.y );
// 		Write( P.BiTangent.z );
// 
// 		// Write emissive mat
// 		Write( P.EmissiveMatID );
// 
// 		// Write SH coefficients (we only write luminance here, we don't have the color info that is provided at runtime)
// 		for ( int i=0; i < 9; i++ )
// 			Write( P.SH[i].x );
// 	}

	// Write the neighbor probes
	Write( m_NeighborProbes.GetCount() );

	// Write nearest/farthest probe distance
	Write( m_NearestNeighborProbeDistance );
	Write( m_FarthestNeighborProbeDistance );

	for ( int i=0; i < m_NeighborProbes.GetCount(); i++ ) {
		const NeighborProbe&	NP = m_NeighborProbes[i];

		// Write probe ID, distance, solid angle, direction
		Write( NP.ProbeID );
		Write( NP.Distance );
		Write( (float) NP.SolidAngle );
		Write( NP.Direction.x );
		Write( NP.Direction.y );
		Write( NP.Direction.z );

		// Write SH coefficients (only luminance here since they're used for the product with the neighbor probe's SH)
		for ( int i=0; i < 9; i++ )
			Write( (float) NP.SH[i] );
	}

	fclose( g_pFile );

// We now save a single file
// // Save probe influence for each scene face
// FileInfo	InfluenceFileName = new FileInfo( Path.Combine( Path.GetDirectoryName( _FileName.FullName ), Path.GetFileNameWithoutExtension( _FileName.FullName ) + ".FaceInfluence" ) );
// using ( FileStream S = InfluenceFileName.Create() )
// 	using ( BinaryWriter W = new BinaryWriter( S ) )
// 	{
// 		for ( U32 FaceIndex=0; FaceIndex < m_MaxFaceIndex; FaceIndex++ )
// 		{
// 			W.Write( (float) (m_ProbeInfluencePerFace.ContainsKey( FaceIndex ) ? m_ProbeInfluencePerFace[FaceIndex] : 0.0) );
// 		}
// 	}
}

void	SHProbeEncoder::SavePixels( const char* _FileName ) const {

	fopen_s( &g_pFile, _FileName, "wb" );
	ASSERT( g_pFile != NULL, "Locked!" );

	Write( U32(CUBE_MAP_SIZE) );

	const Pixel*	P = m_pCubeMapPixels;
	for ( int i=0; i < 6*CUBE_MAP_FACE_SIZE; i++, P++ ) {

		Write( P->pParentSample->Index );

		Write( P->Position.x );
		Write( P->Position.y );
		Write( P->Position.z );
		Write( P->Normal.x );
		Write( P->Normal.y );
		Write( P->Normal.z );

		Write( P->Albedo.x );
		Write( P->Albedo.y );
		Write( P->Albedo.z );
		Write( P->F0.x );
		Write( P->F0.y );
		Write( P->F0.z );

		Write( P->StaticLitColor.x );
		Write( P->StaticLitColor.y );
		Write( P->StaticLitColor.z );
		Write( P->SmoothedStaticLitColor.x );
		Write( P->SmoothedStaticLitColor.y );
		Write( P->SmoothedStaticLitColor.z );

		Write( P->FaceIndex );
		Write( P->EmissiveMatID );
		Write( P->NeighborProbeID );
		Write( P->NeighborProbeDistance );

		Write( P->Importance );
		Write( P->Distance );
		Write( P->SmoothedDistance );
		Write( P->Infinity );
		Write( P->SmoothedInfinity );
	}

	// Write samples
	Write( MAX_PROBE_SAMPLES );
	for ( U32 SampleIndex=0; SampleIndex < MAX_PROBE_SAMPLES; SampleIndex++ ) {
		const Sample&	S = m_pSamples[SampleIndex];

		Write( S.ID );

		Write( S.Position.x );
		Write( S.Position.y );
		Write( S.Position.z );

		Write( S.Normal.x );
		Write( S.Normal.y );
		Write( S.Normal.z );

		Write( S.Tangent.x );
		Write( S.Tangent.y );
		Write( S.Tangent.z );

		Write( S.BiTangent.x );
		Write( S.BiTangent.y );
		Write( S.BiTangent.z );

		Write( S.Radius );

		Write( S.Albedo.x );
		Write( S.Albedo.y );
		Write( S.Albedo.z );
		Write( S.F0.x );
		Write( S.F0.y );
		Write( S.F0.z );

		Write( S.PixelsCount );

		for ( int i=0; i < 9; i++ ) {
			Write( S.SH[i] );
		}
	}

	// Write the emissive surfaces
	Write( U32(0) );
// 	Write( m_EmissiveSurfacesCount );
// 	for ( U32 i=0; i < m_EmissiveSurfacesCount; i++ ) {
// 		Surface&	S = *m_ppEmissiveSurfaces[i];
// 
// 		// Write position, normal, albedo
// 		Write( S.Position.x );
// 		Write( S.Position.y );
// 		Write( S.Position.z );
// 
// 		Write( S.Normal.x );
// 		Write( S.Normal.y );
// 		Write( S.Normal.z );
// 
// 		Write( S.Tangent.x );
// 		Write( S.Tangent.y );
// 		Write( S.Tangent.z );
// 
// 		Write( S.BiTangent.x );
// 		Write( S.BiTangent.y );
// 		Write( S.BiTangent.z );
// 
// 		// Write emissive mat
// 		Write( S.EmissiveMatID );
// 
// 		// Write SH coefficients (we only write luminance here, we don't have the color info that is provided at runtime)
// 		for ( int i=0; i < 9; i++ )
// 			Write( S.SH[i].x );
// 	}

	fclose( g_pFile );
}

#pragma region Computes Sample Pixels by Flood Fill Method

int	DEBUG_PixelIndex = 0;

void	SHProbeEncoder::ComputeFloodFill( float _SpatialDistanceWeight, float _NormalDistanceWeight, float _AlbedoDistanceWeight, float _MinimumImportanceDiscardThreshold ) {
	int	TotalPixelsCount = 6*CUBE_MAP_FACE_SIZE;
 	U32	DiscardThreshold = U32( 0.004f * m_ScenePixelsCount );		// Discard surfaces that contain less than 0.4% of the total amount of scene pixels (arbitrary!)

	// Setup the reference thresholds for pixels' acceptance
//	Pixel.IMPORTANCE_THRESOLD = (float) ((4.0f * Math.PI / CUBE_MAP_FACE_SIZE) / (m_MeanDistance * m_MeanDistance));	// Compute an average solid angle threshold based on average pixels' distance
	Pixel::IMPORTANCE_THRESOLD = (float) (0.1f * _MinimumImportanceDiscardThreshold / (m_MeanHarmonicDistance * m_MeanHarmonicDistance));	// Simply use the mean harmonic distance as a good approximation of important pixels
																									// Pixels that are further or not facing the probe will have less importance...

	DISTANCE_THRESHOLD = 0.30f * _SpatialDistanceWeight;						// 30cm
	ANGULAR_THRESHOLD = acosf( 45.0f * _NormalDistanceWeight * PI / 180.0f );	// 45� (we're very generous here!)
	ALBEDO_HUE_THRESHOLD = 0.04f * _AlbedoDistanceWeight;						// Close colors!
	ALBEDO_RGB_THRESHOLD = 0.32f * _AlbedoDistanceWeight;						// Close colors!


	//////////////////////////////////////////////////////////////////////////
	// Initialize the list of pixels belonging to each sample
	{
		for ( int SampleIndex=0; SampleIndex < MAX_PROBE_SAMPLES; SampleIndex++ ) {
			Sample&	S = m_pSamples[SampleIndex];
			S.PixelsCount = 0;
			S.pPixels = NULL;	// No pixel in that sample at the moment...
		}

		Pixel*	pPixel = m_pCubeMapPixels;
		for ( int PixelIndex=0; PixelIndex < TotalPixelsCount; PixelIndex++, pPixel++ ) {
			pPixel->pNext = pPixel->pParentSample->pPixels;
			pPixel->pParentSample->pPixels = pPixel;
			pPixel->pParentSample->PixelsCount++;
			pPixel->pParentList = NULL;
		}
	}

	//////////////////////////////////////////////////////////////////////////
	// Process each sample
	List< PixelsList >	SamplePixelGroups( m_MaxSamplePixelsCount );	// Worst case scenario: only 1 pixel per group in each sample so as many groups as pixels

	double	GroupImportanceThreshold = _MinimumImportanceDiscardThreshold / MAX_PROBE_SAMPLES;

	m_ValidSamplesCount = 0;
	for ( int SampleIndex=0; SampleIndex < MAX_PROBE_SAMPLES; SampleIndex++ ) {
		Sample&	S = m_pSamples[SampleIndex];

		// Build the lists of pixel groups for that sample
		SamplePixelGroups.Clear();
		Pixel*	pPixel = S.pPixels;
		while ( pPixel != NULL ) {
			if ( pPixel->pParentList == NULL && pPixel->IsFloodFillAcceptable( S ) ) {

				// Propagate from the current pixel and form a coherent group
				PixelsList&	AcceptedPixels = SamplePixelGroups.Append();
				AcceptedPixels.PixelsCount = 0;
				AcceptedPixels.pPixel = NULL;
				AcceptedPixels.Importance = 0.0;

				PixelsList	RejectedPixels;
				FloodFill( S, pPixel, pPixel, AcceptedPixels, RejectedPixels );

				// Restore pixels rejected by that group since they may be useful for another group
				Pixel*	pCurrent = RejectedPixels.pPixel;
				while ( pCurrent != NULL ) {
					Pixel*	pTemp = pCurrent;
					pCurrent = pCurrent->pNextInList;
					pTemp->pParentList = NULL;
					pTemp->pNextInList = NULL;
				}
			}
			pPixel = pPixel->pNext;
		}

		// Keep only the most interesting group
		PixelsList*	pBestGroup = NULL;
		for ( int GroupIndex=0; GroupIndex < SamplePixelGroups.GetCount(); GroupIndex++ ) {
			PixelsList&	Group = SamplePixelGroups[GroupIndex];
						Group.Importance /= Group.PixelsCount;

			if ( pBestGroup == NULL || Group.Importance > pBestGroup->Importance ) {
				pBestGroup = &Group;
			}
		}

		if ( pBestGroup->Importance < GroupImportanceThreshold ) {
			// Discard this sample entirely as it's not important enough
			S.pPixels = NULL;
			S.PixelsCount = 0;
			continue;
		}

		// Build the resulting position, normal, albedo and average direction for the group
		S.pPixels = pBestGroup->pPixel;
		S.PixelsCount = pBestGroup->PixelsCount;

		S.Position = float3::Zero;
		S.Normal = float3::Zero;
		S.Direction = float3::Zero;
		S.Albedo = float3::Zero;

		pPixel = S.pPixels;
		while ( pPixel != NULL ) {
			S.Position = S.Position + pPixel->Position;
			S.Normal = S.Normal + pPixel->Normal;
			S.Direction = S.Direction + pPixel->View;
			S.Albedo = S.Albedo + pPixel->Albedo;
			pPixel = pPixel->pNextInList;
		}

		float	Normalizer = 1.0f / S.PixelsCount;
		S.Position = S.Position * Normalizer;
		S.Normal.Normalize();
		S.Direction.Normalize();
		S.Albedo = S.Albedo * Normalizer;

		// Build the resulting SH for the group
		double	SH[9];
		SH::BuildSHCosineLobe_YUp( S.Normal, SH );

		double	SHNormalizer = SAMPLE_SH_NORMALIZER * S.PixelsCount / S.OriginalPixelsCount;
		for ( int i=0; i < 9; i++ ) {
			S.SH[i] = float( SHNormalizer * SH[i]);
		}

		// Validate the sample
		S.ID = m_ValidSamplesCount;
		m_ppValidSamples[m_ValidSamplesCount++] = &S;
	}



/*
	//////////////////////////////////////////////////////////////////////////
	// 1] Iterate on the list of free pixels that belong to no surface and create new surfaces
	m_AllSurfaces.Clear();

	Surface*	pRegularSurfaces = NULL;
	int			RegularSurfacesCount = 0;
	Surface*	pEmissiveSurfaces = NULL;
	int			EmissiveSurfacesCount = 0;
	for ( int PixelIndex=0; PixelIndex < TotalPixelsCount; PixelIndex++ ) {
		Pixel&	P0 = m_pCubeMapPixels[PixelIndex];

DEBUG_PixelIndex = PixelIndex;

		if ( !P0.IsFloodFillAcceptable() ) {
			continue;	// Not part of the scene geometry or too far away
		}

		// Create a new surface from this pixel
		Surface&	S = m_AllSurfaces.Append();

		S.Position = P0.Position;
		S.Normal = P0.Normal;
		S.View = P0.View;
		S.Distance = P0.Distance;
		S.SmoothedDistance  = P0.SmoothedDistance;
		S.EmissiveMatID = P0.EmissiveMatID;
		S.SetAlbedo( P0.Albedo );
		S.SmoothedStaticLitColor = P0.SmoothedStaticLitColor;


// if ( PixelIndex == 0x2680 )
// 	P0.Albedo.x += 1e-6f;

// if ( P0.IsEmissive )
//  	P0.Albedo.x += 1e-6f;


		// Flood fill adjacent pixels based on a criterion
		S.PixelsCount = 0;
		S.pPixels = NULL;
	 	Pixel*	pRejectedPixels = NULL;

		m_ScanlinePixelIndex = 0;		// VEEERY important line where we reset the pixel index of the pool of flood filled pixels!
		S.Distance2Border = INT_MAX>>1;
		P0.Distance2Border = INT_MAX>>1;
		P0.Distance2Border = 1 + FloodFill( S, &S, &P0, pRejectedPixels );
		ASSERT( m_ScanlinePixelIndex > 0, "Can't have empty surfaces!" );

		// Remove rejected pixels from the surface (we only temporarily marked them to avoid them being processed twice by the flood filler)
		int	RejectedPixelsCount = 0;
		while ( pRejectedPixels != NULL ) {
			pRejectedPixels->pParentSurface = NULL;	// Ready for another round!
			pRejectedPixels->Distance2Border = INT_MAX>>1;
			pRejectedPixels = pRejectedPixels->pNext;
			RejectedPixelsCount++;					// For debugging purpose only...
		}

		// Finalize importance
		S.Importance /= max( 1, S.PixelsCount );

 		if ( S.PixelsCount < DiscardThreshold || S.Importance < Pixel::IMPORTANCE_THRESOLD ) {
			continue;	// This surface is not important enough so don't even bother (I know it could potentially be joined to a larger surface later but I simply don't care)
		}

		// Add the surface to the proper list
		if ( P0.EmissiveMatID == ~0UL ) {
			// One more regular surface
			S.pNext = pRegularSurfaces;
			pRegularSurfaces = &S;
			RegularSurfacesCount++;
		} else {
			// One more emissive surface
			S.pNext = pEmissiveSurfaces;
			pEmissiveSurfaces = &S;
			EmissiveSurfacesCount++;
		}
	}


	//////////////////////////////////////////////////////////////////////////
	// 2] Try and merge surfaces together

	// 2.1) Merge separate emissive surfaces that have the same mat ID together
	Surface*	S0 = pEmissiveSurfaces;
	while ( S0 != NULL ) {
		if ( S0->pParentSurface != NULL ) {

			// Merge with any other surface with same Mat ID
			Surface*	PreviousS1 = S0;
			Surface*	S1 = (Surface*) S0->pNext;
			while ( S1 != NULL ) {
				if ( S1->EmissiveMatID == S0->EmissiveMatID ) {

					// Compute new importance of the merged surfaces
					S0->Importance = (S0->PixelsCount * S0->Importance + S1->PixelsCount * S1->Importance) / (S0->PixelsCount + S1->PixelsCount);
					S0->PixelsCount += S1->PixelsCount;

					// Merge pixels
					Pixel*	pPixel1 = S1->pPixels;
					while ( pPixel1 != NULL ) {
						Pixel*	pTemp = pPixel1;
						pPixel1 = pPixel1->pNext;
						pTemp->pNext = S0->pPixels;
						S0->pPixels = pTemp;
					}

					// Remove the merged surface
					PreviousS1->pNext = S1->pNext;	// Link over that surface
					S1->pParentSurface = S0;		// Mark S0 as its parent so it doesn't get processed again
				}
				S1 = (Surface*) S1->pNext;
			}
		}
		S0 = (Surface*) S0->pNext;
	}

	// 2.2) Merge close enough surfaces
	// TODO?


	//////////////////////////////////////////////////////////////////////////
	// 3] Sort and cull unimportant surfaces
	{
		class	PixelKey : public Pixel::ISortKeyProvider {
		public:	virtual U32	GetKey( const Pixel& _Pixel ) const {
				return ((const Surface&) _Pixel).PixelsCount;
			}
		} Comp;

		Pixel*	pTemp0 = (Pixel*) pRegularSurfaces;
		Pixel*	pTemp1 = (Pixel*) pEmissiveSurfaces;
		Pixel::Sort( pTemp0, Comp, true );
		Pixel::Sort( pTemp1, Comp, true );
		pRegularSurfaces = (Surface*) pTemp0;
		pEmissiveSurfaces = (Surface*) pTemp1;

		// Copy down our selected surfaces
		m_SurfacesCount = 0;
		Surface*	S = pRegularSurfaces;
		while ( S != NULL && m_SurfacesCount < MAX_PROBE_SURFACES ) {
			m_ppSurfaces[m_SurfacesCount++] = S;
			S = (Surface*) S->pNext;
		}

		m_EmissiveSurfacesCount = 0;
		S = pEmissiveSurfaces;
		while ( S != NULL && m_EmissiveSurfacesCount < MAX_PROBE_EMISSIVE_SURFACES ) {
			m_ppEmissiveSurfaces[m_EmissiveSurfacesCount++] = S;
			S = (Surface*) S->pNext;
		}
	}


	//////////////////////////////////////////////////////////////////////////
	// Gather information on each surface
	m_MeanDistance = 0.0;
	m_MeanHarmonicDistance = 0.0;
	m_MinDistance = 1e6;
	m_MaxDistance = 0.0;

	int	SumCardinality = 0;
	for ( U32 SurfaceIndex=0; SurfaceIndex < m_SurfacesCount; SurfaceIndex++ ) {
		Surface&	S = *m_ppSurfaces[SurfaceIndex];
		S.ID = SurfaceIndex;

		// Post-process the pixels to find the one closest to the probe and use it as our centroid
		Pixel*	pBestPixel = S.pPixels;
		float3	AverageNormal = float3::Zero;
		float3	AverageAlbedo = float3::Zero;

		Pixel*	pPixel = S.pPixels;
		while ( pPixel != NULL ) {
			if ( pPixel->Distance < pBestPixel->Distance )
				pBestPixel = pPixel;

			AverageNormal = AverageNormal + pPixel->Normal;
			AverageAlbedo = AverageAlbedo + pPixel->Albedo;

			// Update min/max/avg
			m_MeanDistance += pPixel->Distance;
			m_MinDistance = min( m_MinDistance, pPixel->Distance );
			m_MaxDistance = max( m_MaxDistance, pPixel->Distance );
			m_MeanHarmonicDistance += 1.0 / pPixel->Distance;

			pPixel = pPixel->pNext;
		}

		AverageNormal = AverageNormal / float(S.PixelsCount);
		AverageAlbedo = AverageAlbedo / float(S.PixelsCount);

		S.Position = pBestPixel->Position;	// Our new winner!
		S.Normal = AverageNormal;
		S.SetAlbedo( AverageAlbedo );

		// Count pixels in the surface for statistics
		SumCardinality += S.PixelsCount;

		// Finally, encode SH & find principal axes
		S.EncodeSH();

// Find a faster way!
//		S.FindPrincipalAxes();
	}

	m_MeanHarmonicDistance = SumCardinality / m_MeanHarmonicDistance;
	m_MeanDistance /= SumCardinality;

	// Do the same for emissive surfaces
	for ( U32 SurfaceIndex=0; SurfaceIndex < m_EmissiveSurfacesCount; SurfaceIndex++ ) {
		Surface&	S = *m_ppEmissiveSurfaces[SurfaceIndex];
		S.ID = SurfaceIndex;

		// Post-process the pixels to find the one closest to the probe to use as our centroid
		Pixel*	pBestPixel = S.pPixels;
		Pixel*	pPixel = S.pPixels;
		while ( pPixel != NULL ) {
			if ( pPixel->Distance < pBestPixel->Distance ) {
				pBestPixel = pPixel;
			}
			pPixel = pPixel->pNext;
		}

		S.Position = pBestPixel->Position;	// Our new winner!

		// Finally, encode SH & find principal axes & samples
		S.EncodeEmissiveSH();
	}

	// Generate sampling points for regular surfaces
	U32	TotalSamplesCount = _MaxLightingSamplesCount;
	if ( TotalSamplesCount < m_SurfacesCount ) {
		// Force samples count to match surfaces count!
//		MessageBox( "The amount of samples for the probe was chosen to be " + TotalSamplesCount + " which is inferior to the amount of surfaces, this would mean some surfaces wouldn't even get sampled so the actual amount of samples is at least surface to the amount of surfaces (" + m_Patches.Length + ")", MessageBoxButtons.OK, MessageBoxIcon.Warning );
		TotalSamplesCount = m_SurfacesCount;
	}

	for ( int SurfaceIndex=m_SurfacesCount-1; SurfaceIndex >= 0; SurfaceIndex-- ) {	// We start from the smallest surfaces to ensure they get some samples
		Surface&	S = *m_ppSurfaces[SurfaceIndex];

		U32	SamplesCount = TotalSamplesCount * S.PixelsCount / SumCardinality;
			SamplesCount = max( 1, SamplesCount );				// Ensure we have at least 1 sample no matter what!
			SamplesCount = min( SamplesCount, S.PixelsCount );	// Can't have more samples than pixels!

		S.GenerateSamples( SamplesCount );

		// Reduce the amount of available samples and the count of remaining pixels so the remaining surfaces share the remaining samples...
		TotalSamplesCount -= SamplesCount;
		SumCardinality -= S.PixelsCount;
	}*/
}


#pragma region Flood Fill Algorithm

// This should be a faster version (and much less recursive!) than the original flood fill I wrote some time ago
// The idea here is to process an entire scanline first (going left and right and collecting valid scanline pixels along the way)
//  then for each of these pixels we move up/down and fill the top/bottom scanlines from these new seeds...
//
void	SHProbeEncoder::FloodFill( Sample& _Sample, Pixel* _PreviousPixel, Pixel* _P, PixelsList& _AcceptedPixels, PixelsList& _RejectedPixels ) const {

	static int	RecursionLevel = 0;	// For debugging purpose

	if ( !CheckAndAcceptPixel( _Sample, *_PreviousPixel, *_P, _AcceptedPixels, _RejectedPixels ) )
		return;

	//////////////////////////////////////////////////////////////////////////
	// Check the entire scanline
	int	ScanlineStartIndex = m_ScanlinePixelIndex;
	m_ppScanlinePixelsPool[m_ScanlinePixelIndex++] = _P;	// This pixel is implicitly on the scanline

	{	// Start going right
		CubeMapPixelWalker	P( *this, *_P );
		Pixel*	Previous = _P;
		Pixel*	Current = &P.Right();
		while ( CheckAndAcceptPixel( _Sample, *Previous, *Current, _AcceptedPixels, _RejectedPixels ) ) {
			m_ppScanlinePixelsPool[m_ScanlinePixelIndex++] = Current;
			Previous = Current;
			Current = &P.Right();
		}
	}

	{	// Start going left
		CubeMapPixelWalker	P( *this, *_P );
		Pixel*	Previous = _P;
		Pixel*	Current = &P.Left();
		while ( CheckAndAcceptPixel( _Sample, *Previous, *Current, _AcceptedPixels, _RejectedPixels ) ) {
			m_ppScanlinePixelsPool[m_ScanlinePixelIndex++] = Current;
			Previous = Current;
			Current = &P.Left();
		}
	}

	int	ScanlineEndIndex = m_ScanlinePixelIndex;

	//////////////////////////////////////////////////////////////////////////
	// Recurse into each pixel of the top scanline
	RecursionLevel++;

	for ( int ScanlinePixelIndex=ScanlineStartIndex; ScanlinePixelIndex < ScanlineEndIndex; ScanlinePixelIndex++ ) {
		Pixel*	P = m_ppScanlinePixelsPool[ScanlinePixelIndex];

		CubeMapPixelWalker	Walker( *this, *P );
		Pixel*	Top = &Walker.Up();
		FloodFill( _Sample, P, Top, _AcceptedPixels, _RejectedPixels );
	}

	//////////////////////////////////////////////////////////////////////////
	// Recurse into each pixel of the bottom scanline
	for ( int ScanlinePixelIndex=ScanlineStartIndex; ScanlinePixelIndex < ScanlineEndIndex; ScanlinePixelIndex++ ) {
		Pixel*	P = m_ppScanlinePixelsPool[ScanlinePixelIndex];

		CubeMapPixelWalker	Walker( *this, *P );
		Pixel*	Bottom = &Walker.Down();
		FloodFill( _Sample, P, Bottom, _AcceptedPixels, _RejectedPixels );
	}

	RecursionLevel--;
}

bool	SHProbeEncoder::CheckAndAcceptPixel( Sample& _Sample, Pixel& _PreviousPixel, Pixel& _P, PixelsList& _AcceptedPixels, PixelsList& _RejectedPixels ) const {
	// Start by checking if we can use that pixel
	if ( !_P.IsFloodFillAcceptable( _Sample ) ) {
		return false;
	}

	// Check some additional criterions for a match
	bool	Accepted = false;

	if ( _PreviousPixel.EmissiveMatID != ~0UL && _P.EmissiveMatID != ~0UL ) {
		// Emissive pixels get grouped together
		Accepted = _PreviousPixel.EmissiveMatID == _P.EmissiveMatID;
	} else {
		// First, let's check the angular discrepancy
		float	Dot = _PreviousPixel.Normal | _P.Normal;
		if ( Dot > ANGULAR_THRESHOLD ) {
			// Next, let's check the distance discrepancy
#if 1
			float3	P0 = _PreviousPixel.SmoothedDistance * _PreviousPixel.View;
			float3	P1 = _P.SmoothedDistance * _P.View;
			float	DistanceDiff = (P1 - P0).LengthSq();
			if ( DistanceDiff < DISTANCE_THRESHOLD*DISTANCE_THRESHOLD ) {
#else
			float	DistanceDiff = fabsf( _PreviousPixel.SmoothedDistance - _P.SmoothedDistance );
			float	ToleranceFactor = -(_P.Normal | _P.View);	// Weight by the surface's slope to be more tolerant for slant surfaces
					DistanceDiff *= ToleranceFactor;
			if ( DistanceDiff < DISTANCE_THRESHOLD ) {
#endif

				// Next, let's check the hue discrepancy
// 				float	HueDiff0 = Math.Abs( _PreviousPixel.AlbedoHSL.x - _P.AlbedoHSL.x );
// 				float	HueDiff1 = 6.0f - HueDiff0;
// 				float	HueDiff = Math.Min( HueDiff0, HueDiff1 );
// //						HueDiff *= 0.5f * (_PreviousPixel.AlbedoHSL.y + _P.AlbedoHSL.y);	// Weight by saturation to be less severe with unsaturated colors that can change in hue quite fast
// 						HueDiff *= Math.Max( _PreviousPixel.AlbedoHSL.y, _P.AlbedoHSL.y );	// Weight by saturation to be less severe with unsaturated colors that can change in hue quite fast
// 				if ( HueDiff < ALBEDO_HUE_THRESHOLD )
// 				{
// 					Accepted = true;	// Winner!
// 				}

				// Next, let's check color discrepancy
				// I'm using the simplest metric here...
				float	ColorDiff = (_PreviousPixel.Albedo - _P.Albedo).LengthSq();
				if ( ColorDiff < ALBEDO_RGB_THRESHOLD*ALBEDO_RGB_THRESHOLD ) {
					Accepted = true;	// Winner!
				}
			}
		}
	}

	// Add the pixel to the proper list
	PixelsList&	Target = Accepted ? _AcceptedPixels : _RejectedPixels;
	_P.pNextInList = Target.pPixel;
	_P.pParentList = &Target;
	Target.pPixel = &_P;
	Target.PixelsCount++;
	Target.Importance += _P.Importance * _P.SolidAngle;

	return Accepted;
}

#pragma region Adjacency Walker

// Contains the new cube face index when stepping outside of a cube face through the left/right/top/bottom edge
static int	GoLeft[6] = {
	4,	// Step to +Z
	5,	// Step to -Z
	1,	// Step to -X
	1,	// Step to -X
	1,	// Step to -X
	0,	// Step to +X
};
static int	GoRight[6] = {
	5,	// Step to -Z
	4,	// Step to +Z
	0,	// Step to +X
	0,	// Step to +X
	0,	// Step to +X
	1,	// Step to -X
};
static int	GoDown[6] = {	// Go down means V+1!
	3,	// Step to -Y
	3,	// Step to -Y
	4,	// Step to -Z
	5,	// Step to +Z
	3,	// Step to -Y
	3,	// Step to -Y
};
static int	GoUp[6] = {		// Go up means V-1!
	2,	// Step to +Y
	2,	// Step to +Y
	5,	// Step to -Z
	4,	// Step to +Z
	2,	// Step to +Y
	2,	// Step to +Y
};

// Contains the matrices that indicate how the (U,V) pixel coordinates should be transformed to step from one face to the other
// Transforms arrays are simple 3x2 matrices:
//
//	    | Ru Rv |
//	M = | Du Dv |
//	    | Tu Tv |
//
// Which are used like this:
//
//	UV' = [U V 1] * M
//	 R' = [R R 0] * M
//	 D' = [D D 0] * M
//
// So for example:
//	U' = U * Ru + V * Du + Tu
//	V' = U * Rv + V * Dv + Tv
//
// You have to see the R(ight) and D(own) vectors in M as "what is the new Right/Down vector in this adjacent face?"
// Normally there's a jpeg image right next to this file that explains how I view the whole cubemap thing, following
//	the orientations described in https://msdn.microsoft.com/en-us/library/windows/desktop/bb204881%28v=vs.85%29.aspx
//
const int C = SHProbeEncoder::CUBE_MAP_SIZE;
const int C_ = SHProbeEncoder::CUBE_MAP_SIZE-1;
const int C2 = 2*C-1;

// Transforms are first ordered by cube face index, then by direction:
//	0 = Go left		(UV=[-1, V])
//	1 = Go right	(UV=[ C, V])
//	2 = Go up		(UV=[U, -1])
//	3 = Go down		(UV=[U,  C])
static const int	FaceTransforms[6][4][6] = {

	// Face #0 +X
	{
		// Going left sends us to +Z
		{	 1,  0,
			 0,  1,
			 C,  0 },	// UV' = [C-1, V]
		// Going right sends us to -Z
		{	 1,  0,
			 0,  1,
			-C,  0 },	// UV' = [0, V]
		// Going up sends us to +Y
		{	 0, -1,
			 1,  0,
			 C, C_ },	// UV' = [C-1, C-1-U]
		// Going down sends us to -Y
		{	 0,  1,
			-1,  0,
			C2,  0 },	// UV' = [C-1,U]
	},

	// Face #1 -X
	{
		// Going left sends us to -Z
		{	 1,  0,
			 0,  1,
			 C,  0 },	// UV' = [C-1, V]
		// Going right sends us to +Z
		{	 1,  0,
			 0,  1,
			-C,  0 },	// UV' = [0, V]
		// Going up sends us to +Y
		{	 0,  1,
			-1,  0,
			-1,  0 },	// UV' = [0, U]
		// Going down sends us to -Y
		{	 0, -1,
			 1,  0,
			-C, C_ },	// UV' = [0, C-1-U]
	},

	// Face #2 +Y
	{
		// Going left sends us to -X
		{	 0, -1,
			 1,  0,
			 0, -1 },	// UV' = [V,0]
		// Going right sends us to +X
		{	 0,  1,
			-1,  0,
			C_, -C },	// UV' = [C-1-V, 0]
		// Going up sends us to -Z
		{	-1,  0,
			 0, -1,
			C_, -1 },	// UV' = [C-1-U, 0]
		// Going down sends us to +Z
		{	 1,  0,
			 0,  1,
			 0, -C },	// UV' = [U, 0]
	},

	// Face #3 -Y
	{
		// Going left sends us to -X
		{	 0,  1,
			-1,  0,
			C_,  C },	// UV' = [C-1-V,C-1]
		// Going right sends us to +X
		{	 0, -1,
			 1,  0,
			 0, C2 },	// UV' = [V,C-1]
		// Going up sends us to +Z
		{	 1,  0,
			 0,  1,
			 0,  C },	// UV' = [U, C-1]
		// Going down sends us to -Z
		{	-1,  0,
			 0, -1,
			C_, C2 },	// UV' = [C-1-U, C-1]
	},

	// Face #4 +Z
	{
		// Going left sends us to -X
		{	 1,  0,
			 0,  1,
			 C,  0 },	// UV' = [C-1, V]
		// Going right sends us to +X
		{	 1,  0,
			 0,  1,
			-C,  0 },	// UV' = [0, V]
		// Going up sends us to +Y
		{	 1,  0,
			 0,  1,
			 0,  C },	// UV' = [U, C-1]
		// Going down sends us to -Y
		{	 1,  0,
			 0,  1,
			 0, -C },	// UV' = [U, 0]
	},

	// Face #5 -Z
	{
		// Going left sends us to +X
		{	 1,  0,
			 0,  1,
			 C,  0 },	// UV' = [C-1, V]
		// Going right sends us to -X
		{	 1,  0,
			 0,  1,
			-C,  0 },	// UV' = [0, V]
		// Going up sends us to +Y
		{	-1,  0,
			 0, -1,
			C_, -1 },	// UV' = [C-1-U, 0]
		// Going down sends us to -Y
		{	-1,  0,
			 0, -1,
			C_, C2 },	// UV' = [C-1-U, C-1]
	},
};

void SHProbeEncoder::CubeMapPixelWalker::Set( Pixel& _Pixel ) {
	CubeFaceIndex = _Pixel.CubeFaceIndex;
	pUV[0] = _Pixel.CubeFaceX;
	pUV[1] = _Pixel.CubeFaceY;
	pRight[0] = 1;	pRight[1] = 0;
	pDown[0] = 0;	pDown[1] = 1;
}
SHProbeEncoder::Pixel&	SHProbeEncoder::CubeMapPixelWalker::Get() const {
	int		FinalPixelIndex = CUBE_MAP_FACE_SIZE * CubeFaceIndex + CUBE_MAP_SIZE * pUV[1] + pUV[0];
	Pixel&	Result = Owner.m_pCubeMapPixels[FinalPixelIndex];
	return Result;
}
SHProbeEncoder::Pixel& SHProbeEncoder::CubeMapPixelWalker::Left() {
	GoToAdjacentPixel( -1, 0 );	// U-1
	return Get();
}
SHProbeEncoder::Pixel& SHProbeEncoder::CubeMapPixelWalker::Right() {
	GoToAdjacentPixel( +1, 0 );	// U+1
	return Get();
}
SHProbeEncoder::Pixel& SHProbeEncoder::CubeMapPixelWalker::Up() {
	GoToAdjacentPixel( 0, -1 );	// V-1
	return Get();
}
SHProbeEncoder::Pixel& SHProbeEncoder::CubeMapPixelWalker::Down() {
	GoToAdjacentPixel( 0, +1 );	// V+1
	return Get();
}
void	SHProbeEncoder::CubeMapPixelWalker::TransformUV( const int _Transform[6] ) {
	// Transform position
	int	TempU = pUV[0] * _Transform[0] + pUV[1] * _Transform[2] + _Transform[4];
	int	TempV = pUV[0] * _Transform[1] + pUV[1] * _Transform[3] + _Transform[5];
	pUV[0] = TempU;
	pUV[1] = TempV;
	ASSERT( pUV[0]==0 || pUV[0]==C-1 || pUV[1]==0 || pUV[1]==C-1, "At least one of the coordinates should be at an edge!" );

	// Transform Right & Down directions
	int	pOldRight[2] = { pRight[0], pRight[1] };
	int	pOldDown[2] = { pDown[0], pDown[1] };
	pRight[0] = pOldRight[0] * _Transform[0] + pOldRight[1] * _Transform[2];
	pRight[1] = pOldRight[0] * _Transform[1] + pOldRight[1] * _Transform[3];
	pDown[0] = pOldDown[0] * _Transform[0] + pOldDown[1] * _Transform[2];
	pDown[1] = pOldDown[0] * _Transform[1] + pOldDown[1] * _Transform[3];
}

void	SHProbeEncoder::CubeMapPixelWalker::GoToAdjacentPixel( int _dU, int _dV ) {
	pUV[0] += _dU * pRight[0] + _dV * pDown[0];
	pUV[1] += _dU * pRight[1] + _dV * pDown[1];

	if ( pUV[0] < 0 ) {
		// Stepped out through left side
		TransformUV( FaceTransforms[CubeFaceIndex][0] );
		CubeFaceIndex = GoLeft[CubeFaceIndex];
	}
	if ( pUV[0] >= CUBE_MAP_SIZE ) {
		// Stepped out through right side
		TransformUV( FaceTransforms[CubeFaceIndex][1] );
		CubeFaceIndex = GoRight[CubeFaceIndex];
	}
	if ( pUV[1] < 0 ) {
		// Stepped out through top side
		TransformUV( FaceTransforms[CubeFaceIndex][2] );
		CubeFaceIndex = GoUp[CubeFaceIndex];
	}
	if ( pUV[1] >= CUBE_MAP_SIZE ) {
		// Stepped out through bottom side
		TransformUV( FaceTransforms[CubeFaceIndex][3] );
		CubeFaceIndex = GoDown[CubeFaceIndex];
	}
}

#pragma endregion

#pragma endregion

#pragma endregion


//////////////////////////////////////////////////////////////////////////
//
void	SHProbeEncoder::ReadBackProbeCubeMap( Texture2D& _StagingCubeMap ) {

	m_NearestNeighborProbeDistance = 0.0f;
	m_FarthestNeighborProbeDistance = 0.0f;

	m_MeanDistance = 0.0;
	m_MeanHarmonicDistance = 0.0;
	m_MinDistance = 0.0;
	m_MaxDistance = 0.0;

	m_pScenePixels = NULL;
	m_ScenePixelsCount = 0;

	m_MeanDistance = 0.0;
	m_MeanHarmonicDistance = 0.0;
	m_MinDistance = 1e6;
	m_MaxDistance = 0.0;
	m_BBoxMin =  float3::MaxFlt;
	m_BBoxMax = -float3::MaxFlt;

	int		NegativeImportancePixelsCount = 0;
	for ( int CubeFaceIndex=0; CubeFaceIndex < 6; CubeFaceIndex++ ) {
		Pixel*	pCubeMapPixels = &m_pCubeMapPixels[CubeFaceIndex*CUBE_MAP_FACE_SIZE];

		// Fill up albedo
		D3D11_MAPPED_SUBRESOURCE	Map0 = _StagingCubeMap.Map( 0, 6*0+CubeFaceIndex );
		D3D11_MAPPED_SUBRESOURCE	Map1 = _StagingCubeMap.Map( 0, 6*1+CubeFaceIndex );
		D3D11_MAPPED_SUBRESOURCE	Map2 = _StagingCubeMap.Map( 0, 6*2+CubeFaceIndex );
		D3D11_MAPPED_SUBRESOURCE	Map3 = _StagingCubeMap.Map( 0, 6*3+CubeFaceIndex );

		float4*	pFaceData0 = (float4*) Map0.pData;
		float4*	pFaceData1 = (float4*) Map1.pData;
		float4*	pFaceData2 = (float4*) Map2.pData;
		float4*	pFaceData3 = (float4*) Map3.pData;

		Pixel*	P = pCubeMapPixels;
		for ( int Y=0; Y < CUBE_MAP_SIZE; Y++ )
			for ( int X=0; X < CUBE_MAP_SIZE; X++, P++, pFaceData0++, pFaceData1++, pFaceData2++, pFaceData3++ ) {
				// ==== Read back albedo & unique face ID ====
				float	Red = pFaceData0->x;
				float	Green = pFaceData0->y;
				float	Blue = pFaceData0->z;

// 				// Work with better precision
// 				Red *= PI;
// 				Green *= PI;
// 				Blue *= PI;

				P->SetAlbedo( float3( Red, Green, Blue ) );
				P->FaceIndex = ((U32&) pFaceData0->w);

				// ==== Read back static lighting & emissive material IDs ====
				P->StaticLitColor = *pFaceData2;
				P->EmissiveMatID = ((U32&) pFaceData2->w);

				// ==== Read back position & normal ====
				float	Nx = pFaceData1->x;
				float	Ny = pFaceData1->y;
				float	Nz = pFaceData1->z;
				float	Distance = pFaceData1->w;

				float3	wsPosition( Distance * P->View.x, Distance * P->View.y, Distance * P->View.z );

				P->Position = wsPosition;
				P->Normal.Set( Nx, Ny, Nz );
				P->Normal.Normalize();

				// ==== Read back neighbor probes ID ====
				P->NeighborProbeID = ((U32&) pFaceData3->x);
				P->NeighborProbeDistance = pFaceData3->y;


				// ==== Finalize pixel information ====
				P->Importance = -P->View.Dot( P->Normal ) / (Distance * Distance);
				if ( P->Importance < 0.0 ) {
// P->Normal = -P->Normal;
// P->Importance = -P->View.Dot( P->Normal ) / (Distance * Distance);
// //P->Importance = -P->Importance;
NegativeImportancePixelsCount++;
//					throw new Exception( "WTH?? Negative importance here!" );
				}
				P->Distance = Distance;
				P->Infinity = Distance > Z_INFINITY_TEST;

				if ( P->Infinity )
					continue;	// Not part of the scene's geometry!

				// Link-in a new scene pixel (i.e. not infinity)
				P->pNext = m_pScenePixels;
				m_pScenePixels = P;
				m_ScenePixelsCount++;

				// Update dimensions
				m_MeanDistance += Distance;
				m_MeanHarmonicDistance += 1.0 / Distance;
				m_MinDistance = min( m_MinDistance, Distance );
				m_MaxDistance = max( m_MaxDistance, Distance );
				m_BBoxMin.Max( wsPosition );
				m_BBoxMax.Min( wsPosition );
			}

		_StagingCubeMap.UnMap( 0, 6*3+CubeFaceIndex );
		_StagingCubeMap.UnMap( 0, 6*2+CubeFaceIndex );
		_StagingCubeMap.UnMap( 0, 6*1+CubeFaceIndex );
		_StagingCubeMap.UnMap( 0, 6*0+CubeFaceIndex );
	}

	if ( float(NegativeImportancePixelsCount) / (CUBE_MAP_SIZE * CUBE_MAP_SIZE * 6) > 0.1f )
		ASSERT( false, "More than 10% invalid pixels with inverted normals in that probe!" );

	m_MeanDistance /= (CUBE_MAP_SIZE * CUBE_MAP_SIZE * 6);
	m_MeanHarmonicDistance = (CUBE_MAP_SIZE * CUBE_MAP_SIZE * 6) / m_MeanHarmonicDistance;

	// Perform bilateral filtered smoothing of adjacent distances, static lit colors & infinity values for smoother SH encoding
	for ( int CubeFaceIndex=0; CubeFaceIndex < 6; CubeFaceIndex++ ) {
		Pixel*	pCubeMapPixels = &m_pCubeMapPixels[CubeFaceIndex*CUBE_MAP_FACE_SIZE];
		Pixel*	P = pCubeMapPixels;
		for ( int Y=0; Y < CUBE_MAP_SIZE; Y++ ) {
			for ( int X=0; X < CUBE_MAP_SIZE; X++, P++ ) {

				// Gather the 8 pixels around this one
				CubeMapPixelWalker	Walk( *this, *P );
				Pixel&	P01 = Walk.Up();	// Top
				Pixel&	P02 = Walk.Right();	// Top Right
				Pixel&	P12 = Walk.Down();	// Right
				Pixel&	P22 = Walk.Down();	// Bottom Right
				Pixel&	P21 = Walk.Left();	// Bottom
				Pixel&	P20 = Walk.Left();	// Bottom Left
				Pixel&	P10 = Walk.Up();	// Left
				Pixel&	P00 = Walk.Up();	// Top left

				// Average distance, filtering out the pixels at infinity
				float	SumDistance = !P->Infinity ? P->Distance : 0.0f;
				int		Count = P->Infinity ? 0 : 1;
				if ( !P00.Infinity ) { SumDistance += P00.Distance; Count++; }
				if ( !P01.Infinity ) { SumDistance += P01.Distance; Count++; }
				if ( !P02.Infinity ) { SumDistance += P02.Distance; Count++; }
				if ( !P10.Infinity ) { SumDistance += P10.Distance; Count++; }
				if ( !P12.Infinity ) { SumDistance += P12.Distance; Count++; }
				if ( !P20.Infinity ) { SumDistance += P20.Distance; Count++; }
				if ( !P21.Infinity ) { SumDistance += P21.Distance; Count++; }
				if ( !P22.Infinity ) { SumDistance += P22.Distance; Count++; }
				P->SmoothedDistance = (!P->Infinity && Count > 0) ? SumDistance / Count : Z_INFINITY;

				// Average static lit color
				float3	SumColor = !P->Infinity ? P->StaticLitColor : float3::Zero;
				Count = P->Infinity ? 0 : 1;
				if ( !P00.Infinity ) { SumColor = SumColor + P00.StaticLitColor; Count++; }
				if ( !P01.Infinity ) { SumColor = SumColor + P01.StaticLitColor; Count++; }
				if ( !P02.Infinity ) { SumColor = SumColor + P02.StaticLitColor; Count++; }
				if ( !P10.Infinity ) { SumColor = SumColor + P10.StaticLitColor; Count++; }
				if ( !P12.Infinity ) { SumColor = SumColor + P12.StaticLitColor; Count++; }
				if ( !P20.Infinity ) { SumColor = SumColor + P20.StaticLitColor; Count++; }
				if ( !P21.Infinity ) { SumColor = SumColor + P21.StaticLitColor; Count++; }
				if ( !P22.Infinity ) { SumColor = SumColor + P22.StaticLitColor; Count++; }
				P->SmoothedStaticLitColor = Count > 0 ? SumColor / float(Count) : float3::Zero;

				// Average infinity
				float	SumInfinity  = P->Infinity;
						SumInfinity += P00.Infinity;
						SumInfinity += P01.Infinity;
						SumInfinity += P02.Infinity;
						SumInfinity += P10.Infinity;
						SumInfinity += P12.Infinity;
						SumInfinity += P20.Infinity;
						SumInfinity += P21.Infinity;
						SumInfinity += P22.Infinity;
				P->SmoothedInfinity = SumInfinity / 9;
			}
		}
	}
}

//////////////////////////////////////////////////////////////////////////
//
const double	SHProbeEncoder::Pixel::f0 = 0.5 / PI;
const double	SHProbeEncoder::Pixel::f1 = sqrt(3.0) * SHProbeEncoder::Pixel::f0;
const double	SHProbeEncoder::Pixel::f2 = sqrt(15.0) * SHProbeEncoder::Pixel::f0;
const double	SHProbeEncoder::Pixel::f3 = sqrt(5.0) * 0.5 * SHProbeEncoder::Pixel::f0;

float	SHProbeEncoder::Pixel::IMPORTANCE_THRESOLD = 0.0f;

SHProbeEncoder::Pixel::RadixNode_t*	SHProbeEncoder::Pixel::ms_RadixNodes[2] = { NULL, NULL };
void	SHProbeEncoder::Pixel::Sort( Pixel*& _pList, ISortKeyProvider& _KeyProvider, bool _ReverseSortOnExit ) {
	// Convert linked-list into a sortable list
	Pixel*			pSource = _pList;
	RadixNode_t*	pTarget = ms_RadixNodes[0];
	while ( pSource != NULL ) {
		pTarget->Key = _KeyProvider.GetKey( *pSource );
		pTarget->pPixel = pSource;
		pTarget++;
		pSource = pSource->pNext;
	}

	U32	ElementsCount = pTarget - ms_RadixNodes[0];
	if ( ElementsCount < 2 ) {
		return;	// Nothing to sort here...
	}

	// Sort
	Sort( ElementsCount, ms_RadixNodes[0], ms_RadixNodes[1] );

	// Rebuild sorted linked-list
	if ( _ReverseSortOnExit ) {
		// Reversed, largest to smallest sort
		RadixNode_t*	pNode = ms_RadixNodes[1];
		_pList = NULL;
		for ( U32 i=0; i < ElementsCount; i++, pNode++ ) {
			pNode->pPixel->pNext = _pList;
			_pList = pNode->pPixel;
		}
	} else {
		// Standard, smallest to largest sort
		RadixNode_t*	pNode = ms_RadixNodes[1];
		for ( U32 i=0; i < ElementsCount-1; i++, pNode++ ) {
			pNode->pPixel->pNext = pNode[1].pPixel;
		}
		pNode->pPixel->pNext = NULL;
		_pList = ms_RadixNodes[1]->pPixel;
	}
}

//////////////////////////////////////////////////////////////////////////
// 11-bits Radix sort from Michael Herf (http://stereopsis.com/radix.html)
// (without the floating-point sign flipping because we don't care about that here)
//

// ---- utils for accessing 11-bit quantities
#define _0(x)	(x & 0x7FF)
#define _1(x)	(x >> 11 & 0x7FF)
#define _2(x)	(x >> 22 )

void	SHProbeEncoder::Pixel::Sort( U32 _ElementsCount, RadixNode_t* _pList, RadixNode_t* _pSorted ) {
	U32		i;

	// 3 histograms on the stack:
	const U32	kHist = 2048;		// 11 bits
	U32			b0[kHist * 3];
	U32*		b1 = b0 + kHist;
	U32*		b2 = b1 + kHist;

	for ( i = 0; i < kHist * 3; i++ ) {
		b0[i] = 0;
	}
	//memset(b0, 0, kHist * 12);

	// 1. parallel histogramming pass
	//
	RadixNode_t*	pNode = _pList;
	for ( i=0; i < _ElementsCount; i++, pNode++ ) {
		U32	fi = pNode->Key;
		b0[_0(fi)]++;
		b1[_1(fi)]++;
		b2[_2(fi)]++;
	}
	
	// 2. Sum the histograms -- each histogram entry records the number of values preceding itself.
	{
		U32 sum0 = 0, sum1 = 0, sum2 = 0;
		U32 tsum;
		for ( i=0; i < kHist; i++ ) {

			tsum = b0[i] + sum0;
			b0[i] = sum0 - 1;
			sum0 = tsum;

			tsum = b1[i] + sum1;
			b1[i] = sum1 - 1;
			sum1 = tsum;

			tsum = b2[i] + sum2;
			b2[i] = sum2 - 1;
			sum2 = tsum;
		}
	}

	// byte 0: read/write histogram, write out to sorted
	pNode = _pList;
	for ( i=0; i < _ElementsCount; i++, pNode++ ) {
		U32 fi = pNode->Key;
		U32 pos = _0(fi);
		_pSorted[++b0[pos]] = *pNode;
	}

	// byte 1: read/write histogram, copy sorted -> original
	pNode = _pSorted;
	for ( i=0; i < _ElementsCount; i++, pNode++ ) {
		U32	si = pNode->Key;
		U32	pos = _1(si);
		_pList[++b1[pos]] = *pNode;
	}

	// byte 2: read/write histogram, copy original -> sorted
	pNode = _pList;
	for ( i=0; i < _ElementsCount; i++, pNode++ ) {
		U32 ai = pNode->Key;
		U32 pos = _2(ai);
		_pSorted[++b2[pos]] = *pNode;
	}
}
