/*=========================================================================

  Program:   Insight Segmentation & Registration Toolkit
  Module:    $RCSfile: itkRegionalMaximaImageFilter.h,v $
  Language:  C++
  Date:      $Date: 2008/10/18 00:16:53 $
  Version:   $Revision: 1.1.1.1 $

  Copyright (c) Insight Software Consortium. All rights reserved.
  See ITKCopyright.txt or http://www.itk.org/HTML/Copyright.htm for details.

     This software is distributed WITHOUT ANY WARRANTY; without even 
     the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR 
     PURPOSE.  See the above copyright notices for more information.

=========================================================================*/
#ifndef __itkRegionalMaximaImageFilter_h
#define __itkRegionalMaximaImageFilter_h

#include "itkImageToImageFilter.h"

namespace itk {

/** \class RegionalMaximaImageFilter
 * \brief Produce a binary image where foreground is the regional maxima of the input image
 *
 * Regional maxima are flat zones surounded by pixels of lower value.
 *
 * If the input image is constant, the entire image can be considered as a maxima or not.
 * The desired behavior can be selected with the SetFlatIsMaxima() method.
 * 
 * \author Ga�tan Lehmann. Biologie du D�veloppement et de la Reproduction, INRA de Jouy-en-Josas, France.
 *
 * \sa ValuedRegionalMaximaImageFilter, HConvexImageFilter, RegionalMinimaImageFilter
 * \ingroup ImageEnhancement  MathematicalMorphologyImageFilters
 */
template<class TInputImage, class TOutputImage>
class ITK_EXPORT RegionalMaximaImageFilter : 
    public ImageToImageFilter<TInputImage, TOutputImage>
{
public:
  /** Standard class typedefs. */
  typedef RegionalMaximaImageFilter Self;
  typedef ImageToImageFilter<TInputImage, TOutputImage>
  Superclass;
  typedef SmartPointer<Self>        Pointer;
  typedef SmartPointer<const Self>  ConstPointer;

  /** Some convenient typedefs. */
  typedef TInputImage InputImageType;
  typedef TOutputImage OutputImageType;
  typedef typename InputImageType::Pointer         InputImagePointer;
  typedef typename InputImageType::ConstPointer    InputImageConstPointer;
  typedef typename InputImageType::RegionType      InputImageRegionType;
  typedef typename InputImageType::PixelType       InputImagePixelType;
  typedef typename OutputImageType::Pointer        OutputImagePointer;
  typedef typename OutputImageType::ConstPointer   OutputImageConstPointer;
  typedef typename OutputImageType::RegionType     OutputImageRegionType;
  typedef typename OutputImageType::PixelType      OutputImagePixelType;
  
  /** ImageDimension constants */
  itkStaticConstMacro(InputImageDimension, unsigned int,
                      TInputImage::ImageDimension);
  itkStaticConstMacro(OutputImageDimension, unsigned int,
                      TOutputImage::ImageDimension);

  /** Standard New method. */
  itkNewMacro(Self);  

  /** Runtime information support. */
  itkTypeMacro(RegionalMaximaImageFilter, 
               ImageToImageFilter);

  /**
   * Set/Get whether the connected components are defined strictly by
   * face connectivity or by face+edge+vertex connectivity.  Default is
   * FullyConnectedOff.  For objects that are 1 pixel wide, use
   * FullyConnectedOn.
   */
  itkSetMacro(FullyConnected, bool);
  itkGetConstReferenceMacro(FullyConnected, bool);
  itkBooleanMacro(FullyConnected);
  
  /**
   * Set/Get the value in the output image to consider as "foreground".
   * Defaults to maximum value of PixelType.
   */
  itkSetMacro(ForegroundValue, OutputImagePixelType);
  itkGetConstMacro(ForegroundValue, OutputImagePixelType);

  /**
   * Set/Get the value used as "background" in the output image.
   * Defaults to NumericTraits<PixelType>::NonpositiveMin().
   */
  itkSetMacro(BackgroundValue, OutputImagePixelType);
  itkGetConstMacro(BackgroundValue, OutputImagePixelType);

  /**
   * Set/Get wether a flat image must be considered as a maxima or not.
   * Defaults to true.
   */
  itkSetMacro(FlatIsMaxima, bool);
  itkGetConstMacro(FlatIsMaxima, bool);
  itkBooleanMacro(FlatIsMaxima);

protected:
  RegionalMaximaImageFilter();
  ~RegionalMaximaImageFilter() {};
  void PrintSelf(std::ostream& os, Indent indent) const;

  /** RegionalMaximaImageFilter needs the entire input be
   * available. Thus, it needs to provide an implementation of
   * GenerateInputRequestedRegion(). */
  void GenerateInputRequestedRegion() ;

  /** RegionalMaximaImageFilter will produce the entire output. */
  void EnlargeOutputRequestedRegion(DataObject *itkNotUsed(output));
  
  /** Single-threaded version of GenerateData.  This filter delegates
   * to GrayscaleGeodesicErodeImageFilter. */
  void GenerateData();
  

private:
  RegionalMaximaImageFilter(const Self&); //purposely not implemented
  void operator=(const Self&); //purposely not implemented

  bool m_FullyConnected;
  bool m_FlatIsMaxima;
  OutputImagePixelType m_ForegroundValue;
  OutputImagePixelType m_BackgroundValue;

} ; // end of class

} // end namespace itk
  
#ifndef ITK_MANUAL_INSTANTIATION
#include "itkRegionalMaximaImageFilter.txx"
#endif

#endif

