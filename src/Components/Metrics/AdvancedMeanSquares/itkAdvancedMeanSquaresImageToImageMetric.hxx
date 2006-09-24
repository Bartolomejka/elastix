#ifndef _itkAdvancedMeanSquaresImageToImageMetric_txx
#define _itkAdvancedMeanSquaresImageToImageMetric_txx

#include "itkAdvancedMeanSquaresImageToImageMetric.h"

namespace itk
{

	/**
	* ******************* Constructor *******************
	*/

	template <class TFixedImage, class TMovingImage> 
		AdvancedMeanSquaresImageToImageMetric<TFixedImage,TMovingImage>
		::AdvancedMeanSquaresImageToImageMetric()
	{
    this->SetUseImageSampler(true);
    this->SetUseFixedImageLimiter(false);
    this->SetUseMovingImageLimiter(false);

    this->m_UseNormalization = false;
    this->m_NormalizationFactor = 1.0;

	} // end constructor


  /**
	 * ********************* Initialize ****************************
	 */

  template <class TFixedImage, class TMovingImage>
    void
    AdvancedMeanSquaresImageToImageMetric<TFixedImage,TMovingImage>
    ::Initialize(void) throw ( ExceptionObject )
  {
    /** Initialize transform, interpolator, etc. */
    Superclass::Initialize();

    if ( this->GetUseNormalization() )
    {
      /** Try to guess a normalization factor */
      FixedImagePixelType fixedImageTrueMin;
      FixedImagePixelType fixedImageTrueMax;
      FixedImageLimiterOutputType fixedImageMaxLimit;
      FixedImageLimiterOutputType fixedImageMinLimit;

      this->ComputeFixedImageExtrema(
        this->GetFixedImage(),
        this->GetFixedImageRegion(),
        fixedImageTrueMin,
        fixedImageTrueMax,
        fixedImageMinLimit,
        fixedImageMaxLimit );

      MovingImagePixelType movingImageTrueMin;
      MovingImagePixelType movingImageTrueMax;
      MovingImageLimiterOutputType movingImageMaxLimit;
      MovingImageLimiterOutputType movingImageMinLimit;

      this->ComputeMovingImageExtrema(
        this->GetMovingImage(),
        this->GetMovingImage()->GetBufferedRegion(),
        movingImageTrueMin,
        movingImageTrueMax,
        movingImageMinLimit,
        movingImageMaxLimit );

      const double diff1 = fixedImageTrueMax - movingImageTrueMin;
      const double diff2 = movingImageTrueMax - fixedImageTrueMin;
      const double maxdiff = vnl_math_max( diff1, diff2 ); 

      /** We guess that maxdiff/10 is the maximum average difference 
       * that will be observed.
       * \todo We may involve the standard derivation of the image into
       * this estimate.  */
      this->m_NormalizationFactor = 100.0 / maxdiff / maxdiff;
      
    }
    else
    {
      this->m_NormalizationFactor = 1.0;
    }
   
  } // end Initialize


	/**
	 * ******************* PrintSelf *******************
	 */

	template < class TFixedImage, class TMovingImage> 
		void
		AdvancedMeanSquaresImageToImageMetric<TFixedImage,TMovingImage>
		::PrintSelf(std::ostream& os, Indent indent) const
	{
		Superclass::PrintSelf( os, indent );

	} // end PrintSelf


  /**
	 * *************** EvaluateTransformJacobianInnerProducts ****************
	 */

	template < class TFixedImage, class TMovingImage >
		void
		AdvancedMeanSquaresImageToImageMetric<TFixedImage,TMovingImage>
		::EvaluateTransformJacobianInnerProducts( 
		const TransformJacobianType & jacobian, 
		const MovingImageDerivativeType & movingImageDerivative,
    const MovingImageMaskDerivativeType & movingMaskDerivative,
    DerivativeType & imageJacobian,
    DerivativeType & maskJacobian) const
	{
    typedef typename TransformJacobianType::const_iterator JacobianIteratorType;
    typedef typename DerivativeType::iterator              DerivativeIteratorType;
    JacobianIteratorType jac = jacobian.begin();
    imageJacobian.Fill(0.0);
    maskJacobian.Fill(0.0);
    const unsigned int sizeImageJacobian = imageJacobian.GetSize();
    for ( unsigned int dim = 0; dim < FixedImageDimension; dim++ )
    {
      const double imDeriv = movingImageDerivative[dim];
      const double maskDeriv = movingMaskDerivative[dim];
      DerivativeIteratorType imjac = imageJacobian.begin();
      DerivativeIteratorType maskjac = maskJacobian.begin();
      
      for ( unsigned int mu = 0; mu < sizeImageJacobian ; mu++ )
      {
        (*imjac) += (*jac) * imDeriv;
        (*maskjac) += (*jac) * maskDeriv;
        ++imjac;
        ++maskjac;
        ++jac;
      }
    }
	} // end EvaluateTransformJacobianInnerProducts


	/**
	 * ******************* GetValue *******************
	 */

	template <class TFixedImage, class TMovingImage> 
		typename AdvancedMeanSquaresImageToImageMetric<TFixedImage,TMovingImage>::MeasureType
		AdvancedMeanSquaresImageToImageMetric<TFixedImage,TMovingImage>
		::GetValue( const TransformParametersType & parameters ) const
	{
		itkDebugMacro( "GetValue( " << parameters << " ) " );
		
    /** Initialize some variables */
		this->m_NumberOfPixelsCounted = 0;
    double sumOfMovingMaskValues = 0.0;
    MeasureType measure = NumericTraits< MeasureType >::Zero;

    /** Make sure the transform parameters are up to date. */
		this->SetTransformParameters( parameters );

		/** Update the imageSampler and get a handle to the sample container. */
    this->GetImageSampler()->Update();
    ImageSampleContainerPointer sampleContainer = this->GetImageSampler()->GetOutput();

    /** Create iterator over the sample container. */
    typename ImageSampleContainerType::ConstIterator fiter;
    typename ImageSampleContainerType::ConstIterator fbegin = sampleContainer->Begin();
    typename ImageSampleContainerType::ConstIterator fend = sampleContainer->End();

		/** Loop over the fixed image samples to calculate the mean squares. */
    for ( fiter = fbegin; fiter != fend; ++fiter )
		{
	    /** Read fixed coordinates and initialize some variables */
      const FixedImagePointType & fixedPoint = (*fiter).Value().m_ImageCoordinates;
      RealType movingImageValue; 
      MovingImagePointType mappedPoint;
                  
      /** Transform point and check if it is inside the bspline support region */
      bool sampleOk = this->TransformPoint( fixedPoint, mappedPoint);

      /** Check if point is inside mask */
      RealType movingMaskValue = 0.0;
      if ( sampleOk ) 
      {
        this->EvaluateMovingMaskValueAndDerivative( mappedPoint, movingMaskValue, 0 );
        const double smallNumber1 = 1e-10;
        sampleOk = movingMaskValue > smallNumber1;
      }

      /** Compute the moving image value and check if the point is
      * inside the moving image buffer */
      if ( sampleOk )
      {
        sampleOk = this->EvaluateMovingImageValueAndDerivative(
          mappedPoint, movingImageValue, 0 );
      }
      
      if( sampleOk )
      {
        this->m_NumberOfPixelsCounted++; 
        sumOfMovingMaskValues += movingMaskValue;

        /** Get the fixed image value */
        RealType fixedImageValue = static_cast<double>( (*fiter).Value().m_ImageValue );

				/** The difference squared. */
				const RealType diff = movingImageValue - fixedImageValue; 
				measure += movingMaskValue * diff * diff;
        
			} // end if samplOk

		} // end for loop over the image sample container

    /** Check if enough samples were valid */
    this->CheckNumberOfSamples(
      sampleContainer->Size(), this->m_NumberOfPixelsCounted, sumOfMovingMaskValues );
    
	  measure *= this->m_NormalizationFactor / sumOfMovingMaskValues;

		/** Return the mean squares measure value. */
		return measure;

	} // end GetValue
	

	/**
	 * ******************* GetDerivative *******************
	 */

	template < class TFixedImage, class TMovingImage> 
		void
		AdvancedMeanSquaresImageToImageMetric<TFixedImage,TMovingImage>
		::GetDerivative( const TransformParametersType & parameters,
		DerivativeType & derivative ) const
	{
		/** When the derivative is calculated, all information for calculating
		 * the metric value is available. It does not cost anything to calculate
		 * the metric value now. Therefore, we have chosen to only implement the
		 * GetValueAndDerivative(), supplying it with a dummy value variable. */
		MeasureType dummyvalue = NumericTraits< MeasureType >::Zero;
		this->GetValueAndDerivative( parameters, dummyvalue, derivative );

	} // end GetDerivative


	/**
	 * ******************* GetValueAndDerivative *******************
	 */

	template <class TFixedImage, class TMovingImage>
		void
		AdvancedMeanSquaresImageToImageMetric<TFixedImage,TMovingImage>
		::GetValueAndDerivative( const TransformParametersType & parameters, 
		MeasureType & value, DerivativeType & derivative ) const
	{
		itkDebugMacro("GetValueAndDerivative( " << parameters << " ) ");

    typedef typename DerivativeType::ValueType        DerivativeValueType;
    typedef typename TransformJacobianType::ValueType TransformJacobianValueType;

    /** Initialize some variables */
    this->m_NumberOfPixelsCounted = 0;
    double sumOfMovingMaskValues = 0.0;
    MeasureType measure = NumericTraits< MeasureType >::Zero;
    derivative = DerivativeType( this->m_NumberOfParameters );
		derivative.Fill( NumericTraits< DerivativeValueType >::Zero );

    /** Arrays that store dM(x)/dmu and dMask(x)/dmu */
    DerivativeType imageJacobian( this->m_NonZeroJacobianIndices.GetSize() );
    DerivativeType maskJacobian( this->m_NonZeroJacobianIndices.GetSize() );
   
    DerivativeType numDerivative( this->m_NumberOfParameters );
    DerivativeType denDerivative( this->m_NumberOfParameters );
    numDerivative.Fill( NumericTraits< DerivativeValueType >::Zero );
    denDerivative.Fill( NumericTraits< DerivativeValueType >::Zero );
    
 
		/** Make sure the transform parameters are up to date. */
		this->SetTransformParameters( parameters );
				
    /** Update the imageSampler and get a handle to the sample container. */
    this->GetImageSampler()->Update();
    ImageSampleContainerPointer sampleContainer = this->GetImageSampler()->GetOutput();

    /** Create iterator over the sample container. */
    typename ImageSampleContainerType::ConstIterator fiter;
    typename ImageSampleContainerType::ConstIterator fbegin = sampleContainer->Begin();
    typename ImageSampleContainerType::ConstIterator fend = sampleContainer->End();
		
		/** Loop over the fixed image to calculate the mean squares. */
		for ( fiter = fbegin; fiter != fend; ++fiter )
		{
      /** Read fixed coordinates and initialize some variables */
      const FixedImagePointType & fixedPoint = (*fiter).Value().m_ImageCoordinates;
      RealType movingImageValue; 
      MovingImagePointType mappedPoint;
      MovingImageDerivativeType movingImageDerivative;
            
      /** Transform point and check if it is inside the bspline support region */
      bool sampleOk = this->TransformPoint( fixedPoint, mappedPoint);
      
      /** Check if point is inside mask */
      RealType movingMaskValue = 0.0;
      MovingImageMaskDerivativeType movingMaskDerivative; 
      if ( sampleOk ) 
      {
        this->EvaluateMovingMaskValueAndDerivative(
          mappedPoint, movingMaskValue, &movingMaskDerivative );
        const double movingMaskDerivativeMagnitude = movingMaskDerivative.GetNorm();
        const double smallNumber1 = 1e-10;
        sampleOk = ( movingMaskValue > smallNumber1 ) ||
          ( movingMaskDerivativeMagnitude > smallNumber1 );
      }
    
      /** Compute the moving image value M(T(x)) and derivative dM/dx and check if
       * the point is inside the moving image buffer */
      if ( sampleOk )
      {
        sampleOk = this->EvaluateMovingImageValueAndDerivative( 
          mappedPoint, movingImageValue, &movingImageDerivative );
      }
            
      if( sampleOk )
      {
        this->m_NumberOfPixelsCounted++; 
        sumOfMovingMaskValues += movingMaskValue;

        /** Get the fixed image value */
        RealType fixedImageValue = static_cast<RealType>( (*fiter).Value().m_ImageValue );

        /** Get the TransformJacobian dT/dmu*/
        const TransformJacobianType & jacobian = 
          this->EvaluateTransformJacobian( fixedPoint );
        
        /** compute the innerproducts (dM/dx)^T (dT/dmu) and (dMask/dx)^T (dT/dmu) */
        this->EvaluateTransformJacobianInnerProducts( 
          jacobian, movingImageDerivative, movingMaskDerivative, imageJacobian, maskJacobian );

        /** Compute this pixel's contribution to the measure and derivatives */
        this->UpdateValueAndDerivativeTerms( 
          fixedImageValue, movingImageValue, movingMaskValue,
          imageJacobian, maskJacobian, 
          measure, numDerivative, denDerivative );

			} // end if sampleOk

		} // end for loop over the image sample container

    /** Check if enough samples were valid */
    this->CheckNumberOfSamples(
      sampleContainer->Size(), this->m_NumberOfPixelsCounted, sumOfMovingMaskValues );
       
    const double normal_sum = this->m_NormalizationFactor / sumOfMovingMaskValues;
    measure *= normal_sum;
    const MeasureType measure_N = measure / sumOfMovingMaskValues;

    for( unsigned int i = 0; i < this->m_NumberOfParameters ; i++ )
  	{
	  	derivative[ i ] = numDerivative[i] * normal_sum - denDerivative[i] * measure_N;
		}
		
		/** The return value. */
		value = measure;

	} // end GetValueAndDerivative


  /**
	 * *************** UpdateValueAndDerivativeTerms ***************************
	 */

	template < class TFixedImage, class TMovingImage >
		void
		AdvancedMeanSquaresImageToImageMetric<TFixedImage,TMovingImage>
		::UpdateValueAndDerivativeTerms( 
    RealType fixedImageValue,
    RealType movingImageValue,
    RealType movingMaskValue,
    const DerivativeType & imageJacobian,
    const DerivativeType & maskJacobian,
    MeasureType & measure,
    DerivativeType & numderiv,
    DerivativeType & denderiv  ) const
  {
    typedef typename DerivativeType::ValueType        DerivativeValueType;

    /** The difference squared. */
		const RealType diff = movingImageValue - fixedImageValue; 
    const RealType diffdiff = diff * diff;
		measure += movingMaskValue * diffdiff;
        	  
		/** Calculate the contributions to the derivatives with respect to each parameter. */
    const RealType movmask_diff_2 = movingMaskValue * diff * 2.0;
    if( this->m_NonZeroJacobianIndices.GetSize() == this->m_NumberOfParameters )
		{
      /** Loop over all jacobians */
      typename DerivativeType::const_iterator imjacit = imageJacobian.begin();
      typename DerivativeType::const_iterator maskjacit = maskJacobian.begin();
      typename DerivativeType::iterator numderivit = numderiv.begin();
      typename DerivativeType::iterator denderivit = denderiv.begin();
      for ( unsigned int mu = 0; mu < this->m_NumberOfParameters; ++mu )
      {
        (*numderivit) += movmask_diff_2 * (*imjacit) + diffdiff * (*maskjacit);
        (*denderivit) += (*maskjacit);
        ++imjacit;
        ++maskjacit;
        ++numderivit;
        ++denderivit;
      }
    }
    else
    {
      /** Only pick the nonzero jacobians */
      for ( unsigned int i = 0; i < imageJacobian.GetSize(); ++i)
      {
        const unsigned int index = this->m_NonZeroJacobianIndices[i];
        const DerivativeValueType maskjac = maskJacobian[i];
        numderiv[ index ] += movmask_diff_2 * imageJacobian[i] + diffdiff * maskjac;
        denderiv[ index ] += maskjac;
      }
    }
  } // end UpdateValueAndDerivativeTerms


} // end namespace itk


#endif // end #ifndef _itkAdvancedMeanSquaresImageToImageMetric_txx

