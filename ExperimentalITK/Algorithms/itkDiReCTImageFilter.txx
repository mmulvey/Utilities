/*=========================================================================

  Program:   Advanced Normalization Tools
  Module:    $RCSfile: itkLabelOverlapMeasuresImageFilter.txx,v $
  Language:  C++
  Date:      $Date: $
  Version:   $Revision: $

  Copyright (c) ConsortiumOfANTS. All rights reserved.
  See accompanying COPYING.txt or
 http://sourceforge.net/projects/advants/files/ANTS/ANTSCopyright.txt for details.

     This software is distributed WITHOUT ANY WARRANTY; without even
     the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
     PURPOSE.  See the above copyright notices for more information.

=========================================================================*/
#ifndef __itkDiReCTImageFilter_txx
#define __itkDiReCTImageFilter_txx

#include "itkDiReCTImageFilter.h"

#include "itkAddImageFilter.h"
#include "itkBinaryContourImageFilter.h"
#include "itkBinaryThresholdImageFilter.h"
#include "itkBinaryBallStructuringElement.h"
#include "itkBinaryDilateImageFilter.h"
#include "itkCastImageFilter.h"
#include "itkDiscreteGaussianImageFilter.h"
#include "itkGaussianOperator.h"
#include "itkGradientRecursiveGaussianImageFilter.h"
#include "itkImageDuplicator.h"
#include "itkImageRegionIterator.h"
#include "itkImageRegionConstIteratorWithIndex.h"
#include "itkIterationReporter.h"
#include "itkMaximumImageFilter.h"
#include "itkMultiplyByConstantImageFilter.h"
#include "itkVectorLinearInterpolateImageFunction.h"
#include "itkVectorNeighborhoodOperatorImageFilter.h"
#include "itkWarpImageFilter.h"

namespace itk
{

template<class TInputImage, class TOutputImage>
DiReCTImageFilter<TInputImage, TOutputImage>
::DiReCTImageFilter() :
  m_ThicknessPriorEstimate( 6.0 ),
  m_SmoothingSigma( 1.5 ),
  m_GradientStep( 0.5 ),
  m_NumberOfIntegrationPoints( 10 ),
  m_GrayMatterLabel( 2 ),
  m_WhiteMatterLabel( 3 ),
  m_MaximumNumberOfIterations( 50 ),
  m_ConvergenceThreshold( 0.001 )
{
  this->SetNumberOfRequiredInputs( 3 );
}

template<class TInputImage, class TOutputImage>
DiReCTImageFilter<TInputImage, TOutputImage>
::~DiReCTImageFilter()
{
}

template<class TInputImage, class TOutputImage>
void
DiReCTImageFilter<TInputImage, TOutputImage>
::GenerateData()
{
// Estimation of cortical thickness involves the following steps:
//   1. Diffuse the white matter region
//   2. Calculate gradient of output of step 1.
//   3.

  // Dilate the gray/white matters

  InputImagePointer grayMatter = this->ExtractRegion(
    this->GetSegmentationImage(), this->m_GrayMatterLabel );
  InputImagePointer whiteMatter = this->ExtractRegion(
    this->GetSegmentationImage(), this->m_WhiteMatterLabel );

  typedef AddImageFilter<InputImageType, InputImageType, InputImageType> AdderType;
  typename AdderType::Pointer adder = AdderType::New();
  adder->SetInput1( grayMatter );
  adder->SetInput2( whiteMatter );
  adder->Update();

  InputImagePointer thresholdedRegion = this->ThresholdRegion(
    const_cast<const InputImageType *>( adder->GetOutput() ), 1 );

  typedef BinaryBallStructuringElement<InputPixelType, ImageDimension>
    StructuringElementType;
  typedef BinaryDilateImageFilter<InputImageType, InputImageType,
    StructuringElementType> DilatorType;

  StructuringElementType  structuringElement;
  structuringElement.SetRadius( 1 );
  structuringElement.CreateStructuringElement();

  typename DilatorType::Pointer dilator = DilatorType::New();
  dilator->SetInput( thresholdedRegion );
  dilator->SetKernel( structuringElement );
  dilator->SetDilateValue( 1 );
  dilator->Update();

  InputImagePointer dilatedMatters = dilator->GetOutput();                  // gmgrow

  // Extract the gray and white matter contours

  InputImagePointer dilatedMatterContours = this->ExtractRegionalContours(   // gmsurf
    dilatedMatters, 1 );
  InputImagePointer whiteMatterContoursTmp = this->ExtractRegionalContours(  // bsurf
    this->GetSegmentationImage(), this->m_WhiteMatterLabel );

  typedef CastImageFilter<InputImageType, RealImageType> CasterType;
  typename CasterType::Pointer caster = CasterType::New();
  caster->SetInput( whiteMatterContoursTmp );
  caster->Update();
  RealImagePointer whiteMatterContours = caster->GetOutput();

  // Iterate

    // Initialize fields and images

  VectorType zeroVector( 0.0 );

  RealImagePointer corticalThicknessImage = RealImageType::New();      // finalthickimage
  corticalThicknessImage->CopyInformation( this->GetInput() );
  corticalThicknessImage->SetRegions( this->GetInput()->GetRequestedRegion() );
  corticalThicknessImage->Allocate();
  corticalThicknessImage->FillBuffer( 0.0 );

  VectorImagePointer eulerianField = VectorImageType::New();            // invfield
  eulerianField->CopyInformation( this->GetInput() );
  eulerianField->SetRegions( this->GetInput()->GetRequestedRegion() );
  eulerianField->Allocate();

  VectorImagePointer forwardIncrementalField = VectorImageType::New(); // incrfield
  forwardIncrementalField->CopyInformation( this->GetInput() );
  forwardIncrementalField->SetRegions( this->GetInput()->GetRequestedRegion() );
  forwardIncrementalField->Allocate();

  RealImagePointer hitImage = RealImageType::New();                    // hitimage
  hitImage->CopyInformation( this->GetInput() );
  hitImage->SetRegions( this->GetInput()->GetRequestedRegion() );
  hitImage->Allocate();

  VectorImagePointer integratedField = VectorImageType::New();         // corrfield
  integratedField->CopyInformation( this->GetInput() );
  integratedField->SetRegions( this->GetInput()->GetRequestedRegion() );
  integratedField->Allocate();
  integratedField->FillBuffer( zeroVector );

  VectorImagePointer inverseField = VectorImageType::New();            // invfield
  inverseField->CopyInformation( this->GetInput() );
  inverseField->SetRegions( this->GetInput()->GetRequestedRegion() );
  inverseField->Allocate();

  VectorImagePointer inverseIncrementalField = VectorImageType::New(); // incrinvfield
  inverseIncrementalField->CopyInformation( this->GetInput() );
  inverseIncrementalField->SetRegions( this->GetInput()->GetRequestedRegion() );
  inverseIncrementalField->Allocate();

  RealImagePointer speedImage = RealImageType::New();                  // speed_image
  speedImage->CopyInformation( this->GetInput() );
  speedImage->SetRegions( this->GetInput()->GetRequestedRegion() );
  speedImage->Allocate();

  RealImagePointer thicknessImage = RealImageType::New();                  // speed_image
  thicknessImage->CopyInformation( this->GetInput() );
  thicknessImage->SetRegions( this->GetInput()->GetRequestedRegion() );
  thicknessImage->Allocate();

  RealImagePointer totalImage = RealImageType::New();                  // totalImage
  totalImage->CopyInformation( this->GetInput() );
  totalImage->SetRegions( this->GetInput()->GetRequestedRegion() );
  totalImage->Allocate();

  VectorImagePointer velocityField = VectorImageType::New();           // velofield
  velocityField->CopyInformation( this->GetInput() );
  velocityField->SetRegions( this->GetInput()->GetRequestedRegion() );
  velocityField->Allocate();
  velocityField->FillBuffer( zeroVector );

  IterationReporter reporter( this, 0, 1 );

  this->m_ElapsedIterations = 0;
  while( this->m_ElapsedIterations++ < this->m_MaximumNumberOfIterations ) // && badct < 4 )
    {
    this->m_CurrentConvergenceMeasurement = 0.0;

    forwardIncrementalField->FillBuffer( zeroVector );
    inverseField->FillBuffer( zeroVector );
    inverseIncrementalField->FillBuffer( zeroVector );

    hitImage->FillBuffer( 0.0 );
    totalImage->FillBuffer( 0.0 );
    thicknessImage->FillBuffer( 0.0 );

    // Instantiate the iterators all in one place

    ImageRegionIterator<RealImageType> ItCorticalThicknessImage(
      corticalThicknessImage,
      corticalThicknessImage->GetRequestedRegion() );
    ImageRegionConstIterator<RealImageType> ItGrayMatterProbabilityMap(
      this->GetGrayMatterProbabilityImage(),
      this->GetGrayMatterProbabilityImage()->GetRequestedRegion() );
    ImageRegionIterator<RealImageType> ItHitImage(
      hitImage,
      hitImage->GetRequestedRegion() );
    ImageRegionIterator<VectorImageType> ItForwardIncrementalField(
      forwardIncrementalField,
      forwardIncrementalField->GetRequestedRegion() );
    ImageRegionConstIterator<InputImageType> ItDilatedMatterContours(
      dilatedMatterContours,
      dilatedMatterContours->GetRequestedRegion() );
    ImageRegionIterator<VectorImageType> ItIntegratedField(
      integratedField,
      integratedField->GetRequestedRegion() );
    ImageRegionIterator<VectorImageType> ItInverseIncrementalField(
      inverseIncrementalField,
      inverseIncrementalField->GetRequestedRegion() );
    ImageRegionConstIterator<InputImageType> ItSegmentationImage(
      this->GetSegmentationImage(),
      this->GetSegmentationImage()->GetRequestedRegion() );
    ImageRegionIterator<RealImageType> ItSpeedImage(
      speedImage,
      speedImage->GetRequestedRegion() );
    ImageRegionIterator<RealImageType> ItThicknessImage(
      thicknessImage,
      thicknessImage->GetRequestedRegion() );
    ImageRegionIterator<RealImageType> ItTotalImage(
      totalImage,
      totalImage->GetRequestedRegion() );
    ImageRegionIterator<VectorImageType> ItVelocityField(
      velocityField,
      velocityField->GetRequestedRegion() );
    ImageRegionConstIterator<RealImageType> ItWhiteMatterContours(
      whiteMatterContours,
      whiteMatterContours->GetRequestedRegion() );

    unsigned int integrationPoint = 0;
    while( integrationPoint++ < this->m_NumberOfIntegrationPoints )
      {
      this->ComposeDiffeomorphisms( inverseIncrementalField,
        inverseField, inverseField );

   	  RealImagePointer warpedWhiteMatterProbabilityMap = this->WarpImage( // surfdef
   	    this->GetWhiteMatterProbabilityImage(), inverseField );
   	  RealImagePointer warpedWhiteMatterContours = this->WarpImage(       // thindef
   	    whiteMatterContours, inverseField );
   	  RealImagePointer warpedThicknessImage = this->WarpImage(          // thkdef
   	    thicknessImage, inverseField );

      typedef GradientRecursiveGaussianImageFilter<RealImageType, VectorImageType>
        GradientImageFilterType;
      typename GradientImageFilterType::Pointer gradient =
        GradientImageFilterType::New();
      gradient->SetInput( warpedWhiteMatterProbabilityMap );
      gradient->SetSigma( this->m_SmoothingSigma );
      gradient->Update();                                               // lapgrad2

      // Instantiate the iterators all in one place

      ImageRegionIterator<VectorImageType> ItGradientImage(
        gradient->GetOutput(),
        gradient->GetOutput()->GetRequestedRegion() );
      ImageRegionIterator<VectorImageType> ItInverseField(
        inverseField,
        inverseField->GetRequestedRegion() );
      ImageRegionIterator<RealImageType> ItWarpedThicknessImage(
        warpedThicknessImage,
        warpedThicknessImage->GetRequestedRegion() );
      ImageRegionIterator<RealImageType> ItWarpedWhiteMatterProbabilityMap(
        warpedWhiteMatterProbabilityMap,
        warpedWhiteMatterProbabilityMap->GetRequestedRegion() );
      ImageRegionIterator<RealImageType> ItWarpedWhiteMatterContours(
        warpedWhiteMatterContours,
        warpedWhiteMatterContours->GetRequestedRegion() );

      // Generate speed image

      speedImage->FillBuffer( 0.0 );

      ItGradientImage.GoToBegin();
      ItGrayMatterProbabilityMap.GoToBegin();
      ItSegmentationImage.GoToBegin();
      ItSpeedImage.GoToBegin();
      ItWarpedWhiteMatterProbabilityMap.GoToBegin();

      RealType numberOfGrayMatterVoxels = 0.0;
      while( !ItSegmentationImage.IsAtEnd() )
        {
        if( ItSegmentationImage.Get() == this->m_GrayMatterLabel )
          {
          RealType norm = ( ItGradientImage.Get() ).GetNorm();
          if( norm > 1e-3 && !vnl_math_isnan( norm ) && !vnl_math_isinf( norm ) )
            {
            ItGradientImage.Set( ItGradientImage.Get() / norm );
            }
          else
            {
            ItGradientImage.Set( zeroVector );
            }
          RealType delta = ( ItWarpedWhiteMatterProbabilityMap.Get() -
            ItGrayMatterProbabilityMap.Get() );

          this->m_CurrentConvergenceMeasurement += vnl_math_abs( delta );
          numberOfGrayMatterVoxels++;

          RealType speedValue = -1.0 * delta * ItGrayMatterProbabilityMap.Get() *
            this->m_GradientStep;
          if( vnl_math_isnan( speedValue ) || vnl_math_isinf( speedValue ) )
            {
            speedValue = 0.0;
            }
          ItSpeedImage.Set( speedValue );
          }
        ++ItGradientImage;
        ++ItGrayMatterProbabilityMap;
        ++ItSegmentationImage;
        ++ItSpeedImage;
        ++ItWarpedWhiteMatterProbabilityMap;
        }

      this->m_CurrentConvergenceMeasurement /= numberOfGrayMatterVoxels;

      // Calculate objective function value

      ItDilatedMatterContours.GoToBegin();
      ItForwardIncrementalField.GoToBegin();
      ItGradientImage.GoToBegin();
      ItHitImage.GoToBegin();
      ItIntegratedField.GoToBegin();
      ItInverseField.GoToBegin();
      ItInverseIncrementalField.GoToBegin();
      ItSegmentationImage.GoToBegin();
      ItSpeedImage.GoToBegin();
      ItThicknessImage.GoToBegin();
      ItTotalImage.GoToBegin();
      ItVelocityField.GoToBegin();
      ItWhiteMatterContours.GoToBegin();
      ItWarpedThicknessImage.GoToBegin();
      ItWarpedWhiteMatterContours.GoToBegin();

      while( !ItSegmentationImage.IsAtEnd() )
        {
        ItForwardIncrementalField.Set( ItForwardIncrementalField.Get() +
          ItGradientImage.Get() * ItSpeedImage.Get() );
        if( ItSegmentationImage.Get() == 0 ||
          ( ItDilatedMatterContours.Get() == 0 &&
          ItWhiteMatterContours.Get() == 0 && ItSegmentationImage.Get() !=
          this->m_GrayMatterLabel ) )
          {
          ItIntegratedField.Set( zeroVector );
          ItInverseField.Set( zeroVector );
          ItVelocityField.Set( zeroVector );
          }
        ItInverseIncrementalField.Set( ItVelocityField.Get() );

        if( integrationPoint == 1 )
          {
          ItHitImage.Set( ItWhiteMatterContours.Get() );

          RealType weightedNorm = ( ItIntegratedField.Get() ).GetNorm() *
            ItWhiteMatterContours.Get();

          ItThicknessImage.Set( weightedNorm );
          ItTotalImage.Set( weightedNorm );
          }
        else if( ItSegmentationImage.Get() == this->m_GrayMatterLabel )
          {
          ItHitImage.Set( ItHitImage.Get() +
            ItWarpedWhiteMatterContours.Get() );
          ItTotalImage.Set( ItTotalImage.Get() +
            ItWarpedThicknessImage.Get() );
          }

        ++ItDilatedMatterContours;
        ++ItForwardIncrementalField;
        ++ItGradientImage;
        ++ItHitImage;
        ++ItIntegratedField;
        ++ItInverseField;
        ++ItInverseIncrementalField;
        ++ItSegmentationImage;
        ++ItSpeedImage;
        ++ItThicknessImage;
        ++ItTotalImage;
        ++ItVelocityField;
        ++ItWhiteMatterContours;
        ++ItWarpedThicknessImage;
        ++ItWarpedWhiteMatterContours;
        }
      if( integrationPoint == 1 )
        {
        integratedField->FillBuffer( zeroVector );
        }
      this->InvertDeformationField( inverseField, integratedField, eulerianField );
      this->InvertDeformationField( integratedField, inverseField, eulerianField );
      }
    ItCorticalThicknessImage.GoToBegin();
    ItForwardIncrementalField.GoToBegin();
    ItHitImage.GoToBegin();
    ItSegmentationImage.GoToBegin();
    ItTotalImage.GoToBegin();
    ItVelocityField.GoToBegin();

    while( !ItSegmentationImage.IsAtEnd() )
      {
      ItVelocityField.Set( ItVelocityField.Get() +
        ItForwardIncrementalField.Get() );
      if( ItSegmentationImage.Get() == this->m_GrayMatterLabel )
        {
        RealType thicknessValue = 0.0;
        if( ItHitImage.Get() > 0.001 )
          {
          thicknessValue = ItTotalImage.Get() / ItHitImage.Get();
          if( thicknessValue < 0.0 )
            {
            thicknessValue = 0.0;
            }
          if( thicknessValue > this->m_ThicknessPriorEstimate )
            {
            thicknessValue = this->m_ThicknessPriorEstimate;
            }
          }
        ItCorticalThicknessImage.Set( thicknessValue );
        }

      ++ItCorticalThicknessImage;
      ++ItForwardIncrementalField;
      ++ItHitImage;
      ++ItSegmentationImage;
      ++ItTotalImage;
      ++ItVelocityField;
      }
    velocityField = this->SmoothDeformationField( velocityField,
      this->m_SmoothingSigma );

    reporter.CompletedStep();
    }

  this->SetNthOutput( 0, corticalThicknessImage );
}

template<class TInputImage, class TOutputImage>
typename DiReCTImageFilter<TInputImage, TOutputImage>::InputImagePointer
DiReCTImageFilter<TInputImage, TOutputImage>
::ExtractRegion( InputImageType *segmentationImage,
  unsigned int whichRegion )
{
  typedef BinaryThresholdImageFilter<InputImageType, InputImageType>
    ThresholderType;
  typename ThresholderType::Pointer thresholder = ThresholderType::New();
  thresholder->SetInput( segmentationImage );
  thresholder->SetLowerThreshold( whichRegion );
  thresholder->SetUpperThreshold( whichRegion );
  thresholder->SetInsideValue( 1 );
  thresholder->SetOutsideValue( 0 );
  thresholder->Update();

  InputImagePointer thresholdRegion = thresholder->GetOutput();
  thresholdRegion->Update();
  thresholdRegion->DisconnectPipeline();

  return thresholdRegion;
}

template<class TInputImage, class TOutputImage>
typename DiReCTImageFilter<TInputImage, TOutputImage>::InputImagePointer
DiReCTImageFilter<TInputImage, TOutputImage>
::ExtractRegionalContours( const InputImageType *segmentationImage,
  unsigned int whichRegion )
{
  InputImagePointer thresholdedRegion = this->ThresholdRegion(
    segmentationImage, whichRegion );

  typedef BinaryContourImageFilter<InputImageType, InputImageType>
    ContourFilterType;
  typename ContourFilterType::Pointer contourFilter = ContourFilterType::New();
  contourFilter->SetInput( thresholdedRegion );
  contourFilter->SetFullyConnected( true );
  contourFilter->SetBackgroundValue( 0 );
  contourFilter->SetForegroundValue( 1 );

  InputImagePointer contours = contourFilter->GetOutput();
  contours->Update();
  contours->DisconnectPipeline();
  contours->SetRegions( segmentationImage->GetRequestedRegion() );

  return contours;
}

template<class TInputImage, class TOutputImage>
void
DiReCTImageFilter<TInputImage, TOutputImage>
::ComposeDiffeomorphisms( const VectorImageType *inputField,
  const VectorImageType *warp, VectorImageType *outputField )
{
  VectorType zeroVector( 0.0 );

  typedef VectorLinearInterpolateImageFunction<VectorImageType, RealType>
    InterpolatorType;
  typename InterpolatorType::Pointer interpolator = InterpolatorType::New();
  interpolator->SetInputImage( inputField );

  ImageRegionConstIteratorWithIndex<VectorImageType> ItW( warp,
    warp->GetRequestedRegion() );
  ImageRegionIterator<VectorImageType> ItF( outputField,
    outputField->GetRequestedRegion() );
  for( ItW.GoToBegin(), ItF.GoToBegin(); !ItW.IsAtEnd(); ++ItW, ++ItF )
    {
    typename VectorImageType::IndexType index = ItW.GetIndex();

    PointType point1;
    warp->TransformIndexToPhysicalPoint( index, point1 );

    PointType point2 = point1 + ItW.Get();

    typename InterpolatorType::OutputType displacement;
    if( interpolator->IsInsideBuffer( point2 ) )
      {
      displacement = interpolator->Evaluate( point2 );
      ItF.Set( ( point2 + displacement ) - point1 );
      }
    else
      {
      ItF.Set( zeroVector );
      }
    }
}

template<class TInputImage, class TOutputImage>
typename DiReCTImageFilter<TInputImage, TOutputImage>::RealImagePointer
DiReCTImageFilter<TInputImage, TOutputImage>
::WarpImage( const RealImageType *inputImage,
  const VectorImageType *deformationField )
{
  typedef WarpImageFilter<RealImageType, RealImageType, VectorImageType>
    WarperType;
  typename WarperType::Pointer warper = WarperType::New();
  warper->SetInput( inputImage );
  warper->SetDeformationField( deformationField );
  warper->SetEdgePaddingValue( 0 );
  warper->SetOutputSpacing( inputImage->GetSpacing() );
  warper->SetOutputOrigin( inputImage->GetOrigin() );
  warper->SetOutputDirection( inputImage->GetDirection() );

  RealImagePointer warpedImage = warper->GetOutput();
  warpedImage->Update();
  warpedImage->DisconnectPipeline();

  return warpedImage;
}

template<class TInputImage, class TOutputImage>
void
DiReCTImageFilter<TInputImage, TOutputImage>
::InvertDeformationField( const VectorImageType *deformationField,
  VectorImageType *inverseField, VectorImageType *eulerianField )
{
  typename VectorImageType::SpacingType spacing =
    deformationField->GetSpacing();

  RealType maxNorm = 1.0;
  RealType meanNorm = 1.0;
  unsigned int iteration = 0;
  while( iteration++ < 20 && maxNorm > 0.1 && meanNorm > 0.001 )
    {
    meanNorm = 0.0;
    maxNorm = 0.0;

    this->ComposeDiffeomorphisms(
      deformationField, inverseField, eulerianField );

    ImageRegionIterator<VectorImageType> ItE( eulerianField,
      eulerianField->GetRequestedRegion() );
    for( ItE.GoToBegin(); !ItE.IsAtEnd(); ++ItE )
      {
     	VectorType update = ItE.Get();
     	for( unsigned d = 0; d < ImageDimension; d++ )
     	  {
     	  update[d] /= spacing[d];
     	  }
      RealType norm = update.GetNorm();
     	meanNorm += norm;
     	if( norm > maxNorm )
     	  {
     	  maxNorm = norm;
     	  }
      }
    meanNorm /= static_cast<RealType>(
      eulerianField->GetRequestedRegion().GetNumberOfPixels() );

    RealType epsilon = 0.5;
    if( iteration == 1 )
      {
      epsilon = 0.75;
      }
    RealType normFactor = 1.0;
    for( unsigned int d = 0; d < ImageDimension; d++ )
      {
      normFactor /= spacing[d];
      }

    ImageRegionIterator<VectorImageType> ItI( inverseField,
      inverseField->GetRequestedRegion() );
    for( ItI.GoToBegin(), ItE.GoToBegin(); !ItI.IsAtEnd(); ++ItI, ++ItE )
      {
      VectorType update = -ItE.Get();
      RealType updateNorm = update.GetNorm();

      if( updateNorm * normFactor > epsilon * maxNorm )
        {
        update *= ( epsilon * maxNorm / ( updateNorm * normFactor ) );
        }
      ItI.Set( ItI.Get() + update * epsilon );
      ItE.Set( update );
      }
    }
}

template<class TInputImage, class TOutputImage>
typename DiReCTImageFilter<TInputImage, TOutputImage>::VectorImagePointer
DiReCTImageFilter<TInputImage, TOutputImage>
::SmoothDeformationField( const VectorImageType *inputField,
  const RealType variance )
{
  typedef ImageDuplicator<VectorImageType> DuplicatorType;
  typename DuplicatorType::Pointer duplicator = DuplicatorType::New();
  duplicator->SetInputImage( inputField );
  duplicator->Update();
  VectorImagePointer outputField = duplicator->GetOutput();

  typedef VectorNeighborhoodOperatorImageFilter<VectorImageType,
    VectorImageType> SmootherType;
  typename SmootherType::Pointer smoother = SmootherType::New();

  typedef GaussianOperator<VectorValueType, ImageDimension> GaussianType;
  GaussianType gaussian;
  gaussian.SetVariance( variance );
  gaussian.SetMaximumError( 0.001 );

  for( unsigned int d = 0; d < ImageDimension; d++ )
    {
    gaussian.SetDirection( d );
    gaussian.SetMaximumKernelWidth(
      outputField->GetRequestedRegion().GetSize()[d] );
    gaussian.CreateDirectional();

    smoother->SetOperator( gaussian );
    smoother->SetInput( outputField );

    outputField = smoother->GetOutput();
    outputField->Update();
    outputField->DisconnectPipeline();
    }

  // Ensure the boundary does not move

  RealType weight1 = 1.0;
  if( variance < 0.5 )
    {
    weight1 = 1.0 - 1.0 * ( variance / 0.5 );
    }
  RealType weight2 = 1.0 - weight1;

  VectorType zeroVector( 0.0 );

  typename VectorImageType::IndexType startIndex =
    outputField->GetRequestedRegion().GetIndex();
  typename VectorImageType::SizeType size =
    outputField->GetRequestedRegion().GetSize();

  ImageRegionConstIteratorWithIndex<VectorImageType> ItI( inputField,
    inputField->GetRequestedRegion() );
  ImageRegionIterator<VectorImageType> ItO( outputField,
    outputField->GetRequestedRegion() );
  for( ItI.GoToBegin(), ItO.GoToBegin(); !ItI.IsAtEnd(); ++ItI, ++ItO )
    {
    bool isOnBoundary = false;
    typename VectorImageType::IndexType index = ItO.GetIndex();
    for( unsigned int d = 0; d < ImageDimension; d++ )
      {
      if( index[d] == startIndex[d] ||
        index[d] == ( static_cast<int>( size[d] ) - 1 - startIndex[d] ) )
        {
        isOnBoundary = true;
        break;
        }
      }
    if( isOnBoundary )
      {
      ItO.Set( zeroVector );
      }
    else
      {
      ItO.Set( ItO.Get() * weight1 + ItI.Get() * weight2 );
      }
    }

  return outputField;
}

/**
 * Standard "PrintSelf" method
 */
template <class TInputImage, class TOutputImage>
void
DiReCTImageFilter<TInputImage, TOutputImage>
::PrintSelf( std::ostream& os, Indent indent) const
{
  Superclass::PrintSelf( os, indent );

  std::cout << indent << "Maximum number of iterations = "
    << this->m_MaximumNumberOfIterations << std::endl;
  std::cout << indent << "Thickness prior estimate = "
    << this->m_ThicknessPriorEstimate << std::endl;
  std::cout << indent << "Smoothing sigma = "
    << this->m_SmoothingSigma << std::endl;
  std::cout << indent << "Gradient step = "
    << this->m_GradientStep << std::endl;
}

} // end namespace itk

#endif