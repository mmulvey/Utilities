
#include "itkBSplineControlPointImageFunction.h"
#include "itkBSplineScatteredDataPointSetToImageFilter.h"
#include "itkCollidingFrontsImageFilter.h"
#include "itkHessianToObjectnessMeasureImageFilter.h"
#include "itkImageFileReader.h"
#include "itkImageFileWriter.h"
#include "itkImageRegionConstIteratorWithIndex.h"
#include "itkLinearInterpolateImageFunction.h"
#include "itkMultiScaleHessianBasedMeasureImageFilter.h"
#include "itkNearestNeighborInterpolateImageFunction.h"
#include "itkConfidenceConnectedImageFilter.h"
#include "itkPointSet.h"
#include "itkRescaleIntensityImageFilter.h"
#include "itkSymmetricSecondRankTensor.h"
#include "itkVector.h"

#include "vnl/vnl_cross.h"

#include <string>
#include <vector>
#include <fstream>
#include "Common.h"

int main( int argc, char *argv[] )
{
  if ( argc < 4 )
    {
    std::cout << argv[0] << " inputImage inputSeedImage outputSampledSplineAvantsFile [numberOfIterations=3] [gradientStep=2x1x0.5] [numberOfFittingLevels=4x5x7]" << std::endl;
    std::cout << "Note:  The seed image needs to have two labels (1 and 2) designating the two sets of seed points." << std::endl;
    exit( 1 );
    }

  typedef float RealType;
  typedef float PixelType;

  const unsigned int ImageDimension = 3;
  const unsigned int numberOfSamples = 100;
  unsigned int numberOfIterations = 3;

  std::vector<RealType> gradientSteps;
  gradientSteps.resize( numberOfIterations );
  gradientSteps[0] = 2;
  gradientSteps[1] = 1;
  gradientSteps[2] = 0.5;

  std::vector<unsigned int> numberOfLevels;
  numberOfLevels.resize( numberOfIterations );
  numberOfLevels[0] = 4;
  numberOfLevels[1] = 5;
  numberOfLevels[2] = 7;

  if( argc > 4 )
    {
    numberOfIterations = static_cast<unsigned int>( atoi( argv[4] ) );
    }
  if( argc > 5 )
    {
    gradientSteps = ConvertVector<RealType>( std::string( argv[5] ) );
    if( gradientSteps.size() != numberOfIterations )
      {
      std::cerr << "Error:  The number of elements specified for the gradient steps must be"
        << " equal to the number of iterations." << std::endl;
      }
    }
  if( argc > 6 )
    {
    numberOfLevels = ConvertVector<unsigned int>( std::string( argv[6] ) );
    if( numberOfLevels.size() != numberOfIterations )
      {
      std::cerr << "Error:  The number of elements specified for the number of levels must be"
        << " equal to the number of iterations." << std::endl;
      }
    }

  typedef itk::Vector<RealType, ImageDimension> VectorType;
  typedef itk::Image<PixelType, ImageDimension> ImageType;
  typedef itk::Image<unsigned int, ImageDimension> LabelImageType;

  typedef itk::ImageFileReader<ImageType> ReaderType;
  ReaderType::Pointer reader = ReaderType::New();
  reader->SetFileName( argv[1] );
  reader->Update();

  typedef itk::ImageFileReader<LabelImageType> SeedsReaderType;
  SeedsReaderType::Pointer seedsReader = SeedsReaderType::New();
  seedsReader->SetFileName( argv[2] );
  seedsReader->Update();

  // Connectivity threshold the image and select the regions
  // corresponding to the MRA regions

  typedef itk::ConfidenceConnectedImageFilter<ImageType, ImageType> ThresholderType;
  ThresholderType::Pointer thresholder = ThresholderType::New();
  thresholder->SetInput( reader->GetOutput() );
  thresholder->SetMultiplier( 2 );
  thresholder->SetNumberOfIterations( 25 );
  thresholder->SetReplaceValue( 1.0 );
  thresholder->SetInitialNeighborhoodRadius( 1 );

  itk::ImageRegionConstIteratorWithIndex<LabelImageType> It( seedsReader->GetOutput(),
    seedsReader->GetOutput()->GetLargestPossibleRegion() );

  ImageType::IndexType meanIndex;
  meanIndex.Fill( 0 );

  unsigned long N = 0;

  for( It.GoToBegin(); !It.IsAtEnd(); ++It )
    {
    if( It.Get() == 1 )
      {
      for( unsigned int d = 0; d < ImageDimension; d++ )
        {
        meanIndex[d] += It.GetIndex()[d];
        }
      N++;
      }
    }
  for( unsigned int d = 0; d < ImageDimension; d++ )
    {
    meanIndex[d] = static_cast<int>( meanIndex[d] / N );
    }
  thresholder->AddSeed( meanIndex );

  thresholder->Update();

  itk::ImageRegionIterator<ImageType> ItT( thresholder->GetOutput(),
    thresholder->GetOutput()->GetLargestPossibleRegion() );
  for( ItT.GoToBegin(), It.GoToBegin(); !ItT.IsAtEnd(); ++ItT, ++It )
    {
    if( It.Get() == 3 )
      {
      ItT.Set( 0 );
      }
    }

  // Use colliding fronts to connect the seeds.  Also, create an
  // initial curve based on the seed points and the intermediate
  // point.  We'll use this to parameterize a better guess.

  typedef itk::Image<VectorType, 1> CurveImageType;

  typedef itk::PointSet<VectorType, 1> PointSetType;
  PointSetType::Pointer pointSet = PointSetType::New();
  pointSet->Initialize();

  typedef itk::BSplineScatteredDataPointSetToImageFilter<PointSetType, CurveImageType> BSplinerType;

  typedef itk::CollidingFrontsImageFilter<ImageType, ImageType> FilterType;

  typedef FilterType::NodeContainer NodeContainerType;
  typedef FilterType::NodeType NodeType;
  NodeContainerType::Pointer seeds1 = NodeContainerType::New();
  NodeContainerType::Pointer seeds2 = NodeContainerType::New();
  NodeContainerType::Pointer seeds3 = NodeContainerType::New();

  unsigned long seeds1Counter = 0;
  unsigned long seeds2Counter = 0;
  unsigned long seeds3Counter = 0;

  bool useChrisSamples = false;

  unsigned long count = 0;
  VectorType offset;
  offset.Fill( 0.0 );

  for( It.GoToBegin(); !It.IsAtEnd(); ++It )
    {
    if( It.Get() == 1 )
      {
      LabelImageType::IndexType position = It.GetIndex();

      LabelImageType::PointType point;
      seedsReader->GetOutput()->TransformIndexToPhysicalPoint( position, point );
      for( unsigned int d = 0; d < ImageDimension; d++ )
        {
        offset[d] += point[d];
        }
      count++;

      NodeType node;
      const double value = 0.0;

      node.SetValue( value );
      node.SetIndex( position );

      seeds1->InsertElement( seeds1Counter++, node );
      }
    else if( It.Get() == 2 )
      {
      LabelImageType::IndexType position = It.GetIndex();

      LabelImageType::PointType point;
      seedsReader->GetOutput()->TransformIndexToPhysicalPoint( position, point );
      for( unsigned int d = 0; d < ImageDimension; d++ )
        {
        offset[d] += point[d];
        }
      count++;

      NodeType node;
      const double value = 0.0;

      node.SetValue( value );
      node.SetIndex( position );

      seeds2->InsertElement( seeds2Counter++, node );
      }
    else if( It.Get() == 3 )
      {
      LabelImageType::IndexType position = It.GetIndex();

      LabelImageType::PointType point;
      seedsReader->GetOutput()->TransformIndexToPhysicalPoint( position, point );
      for( unsigned int d = 0; d < ImageDimension; d++ )
        {
        offset[d] += point[d];
        }
      count++;

      NodeType node;
      const double value = 0.0;

      node.SetValue( value );
      node.SetIndex( position );

      seeds3->InsertElement( seeds3Counter++, node );
      }
    else if( !useChrisSamples && It.Get() == 4 )
      {
      useChrisSamples = true;
      }
    }

  typedef itk::NumericTraits<PixelType>::RealType RealPixelType;

  typedef itk::SymmetricSecondRankTensor<RealPixelType, ImageDimension> HessianPixelType;
  typedef itk::Image<HessianPixelType, ImageDimension> HessianImageType;
  typedef itk::HessianToObjectnessMeasureImageFilter<HessianImageType, ImageType> ObjectnessFilterType;
  typedef itk::MultiScaleHessianBasedMeasureImageFilter<ImageType, HessianImageType, ImageType> MultiScaleEnhancementFilterType;

  MultiScaleEnhancementFilterType::Pointer multiScaleEnhancementFilter = MultiScaleEnhancementFilterType::New();
  multiScaleEnhancementFilter->SetInput( thresholder->GetOutput() );
  multiScaleEnhancementFilter->SetSigmaStepMethodToLogarithmic();
  multiScaleEnhancementFilter->SetSigmaMinimum( 1.5  );
  multiScaleEnhancementFilter->SetSigmaMaximum( 3 );
  multiScaleEnhancementFilter->SetNumberOfSigmaSteps( 10 );

  float alpha = 0.5;
  float beta = 0.5;
  float gamma = 5.0;
  bool brightObject = true;

  ObjectnessFilterType::Pointer objectnessFilter = ObjectnessFilterType::New();
  objectnessFilter->SetScaleObjectnessMeasure( false );
  objectnessFilter->SetBrightObject( brightObject );
  objectnessFilter->SetAlpha( alpha );
  objectnessFilter->SetBeta( beta );
  objectnessFilter->SetGamma( gamma );
  objectnessFilter->SetObjectDimension( 1 );

  multiScaleEnhancementFilter->SetHessianToMeasureFilter( objectnessFilter );
  multiScaleEnhancementFilter->Update();

  typedef itk::RescaleIntensityImageFilter<ImageType, ImageType> RescalerType;
  RescalerType::Pointer rescaler = RescalerType::New();
  rescaler->SetInput( multiScaleEnhancementFilter->GetOutput() );
  rescaler->SetOutputMinimum( 0.0 );
  rescaler->SetOutputMaximum( 1.0 );
  rescaler->Update();

  FilterType::Pointer filter = FilterType::New();
  filter->SetInput( rescaler->GetOutput() );
  filter->SetSeedPoints1( seeds1 );
  filter->SetSeedPoints2( seeds2 );
  filter->ApplyConnectivityOn();
  filter->Update();

  PixelType minimumValue = itk::NumericTraits<PixelType>::max();
  PixelType maximumValue = itk::NumericTraits<PixelType>::NonpositiveMin();

  itk::ImageRegionIterator<ImageType> It2( filter->GetOutput(),
    filter->GetOutput()->GetRequestedRegion() );
  for( It2.GoToBegin(); !It2.IsAtEnd(); ++It2 )
    {
    if( It2.Get() > maximumValue )
      {
      maximumValue = It2.Get();
      }
    if( It2.Get() < minimumValue )
      {
      minimumValue = It2.Get();
      }
    }

  PixelType slope = 1.0 / ( minimumValue - maximumValue );
  for( It2.GoToBegin(), It.GoToBegin(); !It2.IsAtEnd(); ++It2, ++It )
    {
    It2.Set( slope * ( It2.Get() - maximumValue ) );
    }

//   typedef itk::ImageFileWriter<ImageType> WriterType;
//   WriterType::Pointer writer = WriterType::New();
//   writer->SetFileName( "filter.nii.gz" );
//   writer->SetInput( filter->GetOutput() );
//   writer->Update();
//
//   exit( 0 );

  for( unsigned int d = 0; d < ImageDimension; d++ )
    {
    offset[d] /= count;
    }

  BSplinerType::WeightsContainerType::Pointer weights = BSplinerType::WeightsContainerType::New();
  weights->Initialize();

  std::vector<VectorType> begSplinePoints;
  std::vector<VectorType> endSplinePoints;

  count = 0;
  for( It.GoToBegin(); !It.IsAtEnd(); ++It )
    {
    if( It.Get() == 1 )
      {
      LabelImageType::IndexType position = It.GetIndex();

      LabelImageType::PointType point;
      seedsReader->GetOutput()->TransformIndexToPhysicalPoint( position, point );
      VectorType bpoint;
      for( unsigned int d = 0; d < ImageDimension; d++ )
        {
        bpoint[d] = point[d] - offset[d];
        }
      begSplinePoints.push_back( bpoint );

      PointSetType::PointType param;
      param[0] = 0.0;
      pointSet->SetPoint( count, param );
      pointSet->SetPointData( count, bpoint );
      weights->InsertElement( count, 1 );
      count++;
      }
    else if( It.Get() == 2 )
      {
      LabelImageType::IndexType position = It.GetIndex();

      LabelImageType::PointType point;
      seedsReader->GetOutput()->TransformIndexToPhysicalPoint( position, point );
      VectorType bpoint;
      for( unsigned int d = 0; d < ImageDimension; d++ )
        {
        bpoint[d] = point[d] - offset[d];
        }
      endSplinePoints.push_back( bpoint );

      PointSetType::PointType param;
      param[0] = 1.0;
      pointSet->SetPoint( count, param );
      pointSet->SetPointData( count, bpoint );
      weights->InsertElement( count, 1 );
      count++;
      }
    else if( It.Get() == 3 )
      {
      LabelImageType::IndexType position = It.GetIndex();

      LabelImageType::PointType point;
      seedsReader->GetOutput()->TransformIndexToPhysicalPoint( position, point );
      VectorType bpoint;
      for( unsigned int d = 0; d < ImageDimension; d++ )
        {
        bpoint[d] = point[d] - offset[d];
        }
      PointSetType::PointType param;
      param[0] = 0.5;
      pointSet->SetPoint( count, param );
      pointSet->SetPointData( count, bpoint );
      weights->InsertElement( count, 1 );
      count++;
      }
    }

  std::vector<VectorType> gradients;
  std::vector<VectorType> normals;
  std::vector<VectorType> binormals;
  std::vector<VectorType> points;

  unsigned int iteration = 0;
  while( iteration++ < numberOfIterations )
    {
    BSplinerType::Pointer bspliner = BSplinerType::New();
    bspliner->SetGenerateOutputImage( false );

    CurveImageType::PointType origin;
    origin.Fill( 0.0 );
    bspliner->SetOrigin( origin );

    CurveImageType::SpacingType spacing;
    spacing[0] = 0.001;
    bspliner->SetSpacing( spacing );

    CurveImageType::SizeType size;
    size[0] = static_cast<unsigned int>( 1.0 / spacing[0] + 1 );
    bspliner->SetSize( size );

    BSplinerType::ArrayType order;
    order[0] = 3;
    bspliner->SetSplineOrder( order );

    BSplinerType::ArrayType ncps;
    ncps[0] = order[0] + 1;
    bspliner->SetNumberOfControlPoints( ncps );

    BSplinerType::ArrayType nlevels;
    nlevels[0] = numberOfLevels[iteration-1];
    bspliner->SetNumberOfLevels( nlevels );

    BSplinerType::ArrayType close;
    close[0] = false;
    bspliner->SetCloseDimension( close );

    bspliner->SetInput( pointSet );
    bspliner->SetPointWeights( weights );
    bspliner->Update();

    typedef itk::BSplineControlPointImageFunction<CurveImageType> BSplineFunctionType;
    BSplineFunctionType::Pointer bsplineFunction = BSplineFunctionType::New();
    bsplineFunction->SetSplineOrder( bspliner->GetSplineOrder() );
    bsplineFunction->SetSpacing( bspliner->GetSpacing() );
    bsplineFunction->SetSize( bspliner->GetSize() );
    bsplineFunction->SetOrigin( bspliner->GetOrigin() );
    bsplineFunction->SetInputImage( bspliner->GetPhiLattice() );

    points.clear();
    binormals.clear();
    normals.clear();
    gradients.clear();

    typedef BSplineFunctionType::GradientType GradientType;

    for( unsigned int n = 0; n < numberOfSamples+1; n++ )
      {
      PointSetType::PointType param;
      param[0] = static_cast<float>( n ) / ( numberOfSamples + 0.0001 );

      VectorType spatialPoint = bsplineFunction->Evaluate( param );
      GradientType gradientMatrix = bsplineFunction->EvaluateGradient( param );

      points.push_back( spatialPoint + offset );

      VectorType grad;
      for( unsigned int d = 0; d < ImageDimension; d++ )
        {
        grad[d] = gradientMatrix[0][d];
        }
      grad.Normalize();
      gradients.push_back( grad );
      }

    for( unsigned int n = 0; n < gradients.size(); n++ )
      {
      VectorType normal;

      PointSetType::PointType param;
      param[0] = static_cast<float>( n ) / ( numberOfSamples + 0.0001 );

      for( unsigned int d = 0; d < ImageDimension; d++ )
        {
        normal[d] = bsplineFunction->EvaluateHessian( param, d )[0][0];
        }
      normal.Normalize();
      normals.push_back( normal );
      }

    for( unsigned int n = 0; n < gradients.size(); n++ )
      {
      vnl_vector<RealType> tmp = vnl_cross_3d( normals[n].GetVnlVector(), gradients[n].GetVnlVector() );
      tmp.normalize();
      VectorType binormal;
      binormal.SetVnlVector( tmp );

      binormals.push_back( binormal );
      }

    normals.clear();
    for( unsigned int n = 0; n < binormals.size(); n++ )
      {
      vnl_vector<RealType> tmp = vnl_cross_3d( binormals[n].GetVnlVector(), gradients[n].GetVnlVector() );
      tmp.normalize();
      VectorType normal;
      normal.SetVnlVector( tmp );

      normals.push_back( normal );
      }
    if( iteration == numberOfIterations )
      {
      break;
      }

    // Now traverse the curve and sample the normal plane

    typedef itk::LinearInterpolateImageFunction<ImageType> InterpolatorType;
    InterpolatorType::Pointer interpolator = InterpolatorType::New();
    interpolator->SetInputImage( filter->GetOutput() );

    typedef itk::NearestNeighborInterpolateImageFunction<LabelImageType> NNInterpolatorType;
    NNInterpolatorType::Pointer interpolator2 = NNInterpolatorType::New();
    interpolator2->SetInputImage( seedsReader->GetOutput() );

    RealType voxelSpacingFactor = 0.0;
    for( unsigned int d = 0; d < ImageDimension; d++ )
      {
      voxelSpacingFactor += vnl_math_sqr( filter->GetOutput()->GetSpacing()[d] );
      }
    voxelSpacingFactor = vcl_sqrt( voxelSpacingFactor );

    pointSet->Initialize();
    weights->Initialize();

    count = 0;

    for( unsigned int n = 0; n < points.size(); n++ )
      {
      PointSetType::PointType param;
      param[0] = static_cast<float>( n ) / ( points.size() + 0.0001 );

      for( RealType u = -gradientSteps[iteration-1]; u <= gradientSteps[iteration-1]; u += ( gradientSteps[iteration-1] * 0.2 ) )
        {
        for( RealType v = -gradientSteps[iteration-1]; v <= gradientSteps[iteration-1]; v += ( gradientSteps[iteration-1] * 0.2 ) )
          {
          if( vcl_sqrt( vnl_math_sqr( u ) + vnl_math_sqr( v ) ) > 3.0 )
            {
            continue;
            }

          VectorType tmp = points[n] + normals[n] * u * voxelSpacingFactor + binormals[n] * v * voxelSpacingFactor;

          InterpolatorType::PointType samplePoint;
          for( unsigned int d = 0; d < ImageDimension; d++ )
            {
            samplePoint[d] = tmp[d];
            }
          if( ! interpolator->IsInsideBuffer( samplePoint ) ||
              interpolator2->Evaluate( samplePoint ) == 3 )
            {
            continue;
            }

          RealType weight = 1.0;
          if( useChrisSamples )
            {
            if( interpolator2->Evaluate( samplePoint ) != 4 )
              {
              weight = 0.0;
              }
            }
          else
            {
            weight = interpolator->Evaluate( samplePoint );
            }

          if( weight <= 0.0 )
            {
            continue;
            }

          weights->InsertElement( count, weight );

          pointSet->SetPoint( count, param );
          pointSet->SetPointData( count, tmp - offset );
          count++;
          }
        }
      }

    PointSetType::PointType param;
    param[0] = 0.0;
    for( unsigned int n = 0; n < begSplinePoints.size(); n++ )
      {
      pointSet->SetPoint( count, param );
      pointSet->SetPointData( count, begSplinePoints[n] );
      weights->InsertElement( count, 1.0 );
      count++;
      }

    param[0] = 1.0;
    for( unsigned int n = 0; n < endSplinePoints.size(); n++ )
      {
      pointSet->SetPoint( count, param );
      pointSet->SetPointData( count, endSplinePoints[n] );
      weights->InsertElement( count, 1.0 );
      count++;
      }

    bspliner->SetInput( pointSet );
    bspliner->SetPointWeights( weights );
    bspliner->Update();
    }

  std::ofstream str( argv[3] );

  str << "0 0 0 0" << std::endl;
  for( unsigned int n = 0; n < points.size(); n++ )
    {
    VectorType curvePoint = points[n];
    for( unsigned int d = 0; d < ImageDimension; d++ )
      {
      str << curvePoint[d] << " ";
      }
    str << "1" << std::endl;
    }
  str << "0 0 0 0" << std::endl;

//   typedef itk::ImageFileWriter<ImageType> WriterType;
//   WriterType::Pointer writer = WriterType::New();
//   writer->SetFileName( argv[3] );
//   writer->SetInput( filter->GetOutput() );
//   writer->Update();

  return 0;
}


