#include "test_cuda.h"
#include "CudaManager.h"
#include "SourceFFT_Cuda.h"
#include "LocalCodesFFT_Cuda.h"
#include "ContinuousPhaseCarrier.h"
#include "GoldCodeGenerator.h"
#include "CodeModulator_BPSK.h"
#include "Cuda_Operators.h"
#include "Cuda_IndexTransforms.h"
#include "WsgcException.h"
#include "Cuda_StridedRange.h"

#include <thrust/device_vector.h>
#include <thrust/host_vector.h>
#include <thrust/copy.h>
#include <thrust/transform.h>
#include <thrust/iterator/counting_iterator.h>
#include <thrust/iterator/transform_iterator.h>
#include <thrust/transform_reduce.h>
#include <cufft.h>
#include <cublas_v2.h>
#include <cutil_inline.h>    // includes cuda.h and cuda_runtime_api.h


test_cuda::test_cuda(options_t& options) :
	_options(options)
{
	_cuda_manager = new CudaManager(options.nb_message_symbols, options.nb_pilot_prns);
    _cuda_manager->diagnose();
    std::ostringstream cuda_os;
    _cuda_manager->dump(cuda_os);
    std::cout << cuda_os.str() << std::endl << std::endl;
}


test_cuda::~test_cuda()
{
	delete _cuda_manager;
}


void test_cuda::test1()
{
    ContinuousPhaseCarrier source_oscillator(_options.f_sampling, _options.fft_N);
    source_oscillator.make_next_samples(_options.f_chip);

    SourceFFT_Cuda source_fft(_options.f_sampling, _options.f_chip, _options.fft_N, _options.freq_step_division);

    thrust::device_vector<cuComplex>& d_fft_source = source_fft.do_fft(source_oscillator.get_samples());
    thrust::host_vector<cuComplex> h_fft_source(_options.freq_step_division*_options.fft_N);
    thrust::copy(d_fft_source.begin(), d_fft_source.end(), h_fft_source.begin());

    thrust::host_vector<cuComplex>::iterator it = h_fft_source.begin();
    const thrust::host_vector<cuComplex>::iterator it_end = h_fft_source.end();
    unsigned int i = 0;

    for (;it != it_end; ++it, i++)
    {
    	wsgc_float module = (*it).x * (*it).x + (*it).y * (*it).y;
    	std::cout << i/_options.fft_N << "," << i % _options.fft_N << ": " << module << std::endl;
    }
}


void test_cuda::test2(wsgc_complex *message_samples, GoldCodeGenerator& gc_generator, CodeModulator_BPSK& code_modulator)
{
	std::cout << "--- input:" << std::endl;

	for (unsigned int i=0; i < _options.fft_N; i++)
	{
		std::cout << i << ": (" << message_samples[i].real() << "," << message_samples[i].imag() << ")" << std::endl;
	}

	SourceFFT_Cuda source_fft(_options.f_sampling, _options.f_chip, _options.fft_N, _options.freq_step_division);
	thrust::device_vector<cuComplex>& d_fft_source = source_fft.do_fft(message_samples);

	LocalCodesFFT_Cuda local_codes(code_modulator, gc_generator, _options.f_sampling, _options.f_chip, _options.prn_list);

	thrust::device_vector<cuComplex> d_result(_options.fft_N);
	const thrust::device_vector<cuComplex>& d_local_codes = local_codes.get_local_codes();

	std::cout << d_fft_source.size() << ":" << d_local_codes.size() << std::endl;

	thrust::transform(d_fft_source.begin(), d_fft_source.begin()+_options.fft_N, d_local_codes.begin(), d_result.begin(), cmulc_functor2());

	cufftHandle ifft_plan;
	cufftResult_t fft_stat = cufftPlan1d(&ifft_plan, _options.fft_N, CUFFT_C2C, 1);
	thrust::device_vector<cuComplex> d_ifft(_options.fft_N);

    if (cufftExecC2C(ifft_plan, thrust::raw_pointer_cast(&d_result[0]), thrust::raw_pointer_cast(&d_ifft[0]), CUFFT_INVERSE) != CUFFT_SUCCESS)
    {
        throw WsgcException("CUFFT Error: Failed to do IFFT of source*local_code");
    }

    thrust::host_vector<cuComplex> h_ifft(_options.fft_N);
    thrust::host_vector<cuComplex>::iterator it = h_ifft.begin();
    const thrust::host_vector<cuComplex>::iterator it_end = h_ifft.end();
    unsigned int i = 0;

    thrust::copy(d_result.begin(), d_result.begin()+_options.fft_N, h_ifft.begin());

    std::cout << "--- input IFFT:" << std::endl;

    for (;it != it_end; ++it, i++)
    {
    	std::cout << i << ": (" << (*it).x << "," << (*it).y << ")" << std::endl;
    }

    thrust::copy(d_ifft.begin(), d_ifft.end(), h_ifft.begin());

    it = h_ifft.begin();
    i = 0;

    std::cout << "--- result IFFT:" << std::endl;

    for (;it != it_end; ++it, i++)
    {
    	std::cout << i << ": (" << (*it).x << "," << (*it).y << ")" << std::endl;
    }

	cufftDestroy(ifft_plan);
}


void test_cuda::test3(wsgc_complex *message_samples, GoldCodeGenerator& gc_generator, CodeModulator_BPSK& code_modulator)
{
	SourceFFT_Cuda source_fft(_options.f_sampling, _options.f_chip, _options.fft_N, _options.freq_step_division);
	thrust::device_vector<cuComplex>& d_source_block = source_fft.do_fft(message_samples);

	LocalCodesFFT_Cuda local_codes(code_modulator, gc_generator, _options.f_sampling, _options.f_chip, _options.prn_list);
	const thrust::device_vector<cuComplex>& d_local_codes = local_codes.get_local_codes();

	thrust::device_vector<cuComplex> d_ifft_in(_options.fft_N*_options.nb_f_bins*_options.freq_step_division*2*_options.nb_batch_prns);
	thrust::device_vector<cuComplex> d_ifft_out(_options.fft_N*_options.nb_f_bins*_options.freq_step_division*2*_options.nb_batch_prns);
	thrust::host_vector<cuComplex> h_ifft_out(_options.fft_N*_options.nb_f_bins*_options.freq_step_division*2*_options.nb_batch_prns);

	unsigned int pilot_prn_index = 0;
	unsigned int prn_position = 0;

    thrust::for_each(
        thrust::make_zip_iterator(
            thrust::make_tuple(
                thrust::make_permutation_iterator(d_source_block.begin(), thrust::make_transform_iterator(thrust::make_counting_iterator(0), transpose_index_A(_options.fft_N, _options.freq_step_division))),
                thrust::make_permutation_iterator(d_local_codes.begin() + pilot_prn_index*_options.fft_N, thrust::make_transform_iterator(thrust::make_counting_iterator(0), transpose_index_B(_options.fft_N, _options.freq_step_division, _options.nb_f_bins))),
                thrust::make_permutation_iterator(d_ifft_in.begin(), thrust::make_transform_iterator(thrust::make_counting_iterator(0), transpose_index_C(_options.nb_batch_prns, prn_position)))
            )
        ),
        thrust::make_zip_iterator(
            thrust::make_tuple(
				thrust::make_permutation_iterator(d_source_block.begin(), thrust::make_transform_iterator(thrust::make_counting_iterator(0)+(_options.fft_N*_options.freq_step_division*_options.nb_f_bins), transpose_index_A(_options.fft_N, _options.freq_step_division))),
				thrust::make_permutation_iterator(d_local_codes.begin() + (pilot_prn_index+1)*_options.fft_N, thrust::make_transform_iterator(thrust::make_counting_iterator(0)+(_options.fft_N*_options.freq_step_division*_options.nb_f_bins), transpose_index_B(_options.fft_N, _options.freq_step_division, _options.nb_f_bins))),
				thrust::make_permutation_iterator(d_ifft_in.begin(), thrust::make_transform_iterator(thrust::make_counting_iterator(0)+(_options.fft_N*_options.freq_step_division*_options.nb_f_bins), transpose_index_C(_options.nb_batch_prns, prn_position)))
            )
        ),
        cmulc_functor()
    );

    cufftHandle ifft_plan;

	int n[1];                                       //!< CUFFT Plan FFT size parameter
	int inembed[1];                                 //!< CUFFT Plan parameter
	int onembed[1];                                 //!< CUFFT Plan parameter

    n[0] = _options.fft_N;
    inembed[0] = _options.fft_N;
    onembed[0] = _options.fft_N;

    cufftResult_t fft_stat = cufftPlanMany(&ifft_plan, 1, n,
		inembed, 2*_options.nb_batch_prns, 2*_options.nb_batch_prns*_options.fft_N,
		onembed, 2*_options.nb_batch_prns, 2*_options.nb_batch_prns*_options.fft_N,
		CUFFT_C2C, _options.nb_f_bins*_options.freq_step_division);

    if (fft_stat != CUFFT_SUCCESS)
    {
    	std::ostringstream err_os;
    	err_os << "CUFFT Error: Unable to create plan for pilot IFFT RC=" << fft_stat;
    	throw WsgcException(err_os.str());
    }

    if (cufftExecC2C(ifft_plan,
    		         thrust::raw_pointer_cast(&d_ifft_in[prn_position]),
    		         thrust::raw_pointer_cast(&d_ifft_out[prn_position]),
    		         CUFFT_INVERSE) != CUFFT_SUCCESS)
    {
    	throw WsgcException("CUFFT Error: Failed to execute IFFT");
    }

    thrust::copy(d_ifft_out.begin(), d_ifft_out.end(), h_ifft_out.begin());

    std::cout << "--- f0.0 :" << std::endl;

    unsigned int f_shift = 0;

    /*
    for (unsigned int i=0; i < _options.fft_N; i++)
    {
    	unsigned int mi = f_shift + prn_position + i*2*_options.nb_batch_prns;
    	std::cout << i << ">" << mi << ": (" << h_ifft_out[mi].x << "," << h_ifft_out[mi].y << ")" << std::endl;
    }
    */

    std::cout << "--- f1.0 :" << std::endl;

    f_shift = _options.fft_N * 2 *_options.nb_batch_prns * _options.freq_step_division;

    for (unsigned int i=0; i < _options.fft_N; i++)
    {
    	unsigned int mi = f_shift + prn_position + i*2*_options.nb_batch_prns;
    	std::cout << i << ">" << mi << ": (" << h_ifft_out[mi].x << "," << h_ifft_out[mi].y << ")" << std::endl;
    }

    std::cout << "--- f1.1 :" << std::endl;

    f_shift = _options.fft_N * 2 *_options.nb_batch_prns * (_options.freq_step_division+1);

    for (unsigned int i=0; i < _options.fft_N; i++)
    {
    	unsigned int mi = f_shift + prn_position + i*2*_options.nb_batch_prns;
    	std::cout << i << ">" << mi << ": (" << h_ifft_out[mi].x << "," << h_ifft_out[mi].y << ")" << std::endl;
    }

    cublasHandle_t cublas_handle;

	cublasStatus_t stat = cublasCreate(&cublas_handle);
	if (stat != CUBLAS_STATUS_SUCCESS)
	{
		throw WsgcException("CUBLAS Error: Failed to initialize library");
	}

    int cublas_max_index;

	stat = cublasIcamax(cublas_handle, (_options.fft_N*_options.freq_step_division*_options.nb_f_bins),
			thrust::raw_pointer_cast(&d_ifft_out[prn_position]),
			2*_options.nb_batch_prns, &cublas_max_index);

	if (stat != CUBLAS_STATUS_SUCCESS)
	{
		std::ostringstream err_os;
		err_os << "CUBLAS Error: cublasIcamax failed with RC=" << stat;
		std::cout << err_os.str() << std::endl;
		throw WsgcException(err_os.str());
	}

	cublas_max_index--; // Cublas index are 1 based

    cuComplex z = d_ifft_out[prn_position + cublas_max_index*2*_options.nb_batch_prns];
    std::cout << prn_position << ": IFFT max: " << prn_position + cublas_max_index*2*_options.nb_batch_prns << " : " << mag_algebraic_functor()(z) << std::endl;
    std::cout << cublas_max_index % _options.fft_N << std::endl;
    std::cout << cublas_max_index / _options.fft_N << std::endl;

    cufftDestroy(ifft_plan);
    cublasDestroy(cublas_handle);
}


void test_cuda::test4(wsgc_complex *message_samples, GoldCodeGenerator& gc_generator, CodeModulator_BPSK& code_modulator)
{
	SourceFFT_Cuda source_fft(_options.f_sampling, _options.f_chip, _options.fft_N, _options.freq_step_division);
	thrust::device_vector<cuComplex>& d_source_block = source_fft.do_fft(message_samples);

	LocalCodesFFT_Cuda local_codes(code_modulator, gc_generator, _options.f_sampling, _options.f_chip, _options.prn_list);
	const thrust::device_vector<cuComplex>& d_local_codes = local_codes.get_local_codes();

	thrust::device_vector<cuComplex> d_ifft_in(_options.fft_N*_options.nb_f_bins*_options.freq_step_division*2*_options.nb_batch_prns);
	thrust::device_vector<cuComplex> d_ifft_out(_options.fft_N*_options.nb_f_bins*_options.freq_step_division*2*_options.nb_batch_prns);
	thrust::host_vector<cuComplex> h_ifft_out(_options.fft_N*_options.nb_f_bins*_options.freq_step_division*2*_options.nb_batch_prns);

	unsigned int pilot_prn_index = 0;
	unsigned int prn_position = 1;

    thrust::for_each(
        thrust::make_zip_iterator(
            thrust::make_tuple(
                thrust::make_permutation_iterator(d_source_block.begin(), thrust::make_transform_iterator(thrust::make_counting_iterator(0), transpose_index_A(_options.fft_N, _options.freq_step_division))),
                thrust::make_permutation_iterator(d_local_codes.begin() + pilot_prn_index*_options.fft_N, thrust::make_transform_iterator(thrust::make_counting_iterator(0), transpose_index_B(_options.fft_N, _options.freq_step_division, _options.nb_f_bins))),
                thrust::make_permutation_iterator(d_ifft_in.begin(), thrust::make_transform_iterator(thrust::make_counting_iterator(0), transpose_index_C(_options.nb_batch_prns, prn_position)))
            )
        ),
        thrust::make_zip_iterator(
            thrust::make_tuple(
				thrust::make_permutation_iterator(d_source_block.begin(), thrust::make_transform_iterator(thrust::make_counting_iterator(0)+(_options.fft_N*_options.freq_step_division*_options.nb_f_bins), transpose_index_A(_options.fft_N, _options.freq_step_division))),
				thrust::make_permutation_iterator(d_local_codes.begin() + (pilot_prn_index+1)*_options.fft_N, thrust::make_transform_iterator(thrust::make_counting_iterator(0)+(_options.fft_N*_options.freq_step_division*_options.nb_f_bins), transpose_index_B(_options.fft_N, _options.freq_step_division, _options.nb_f_bins))),
				thrust::make_permutation_iterator(d_ifft_in.begin(), thrust::make_transform_iterator(thrust::make_counting_iterator(0)+(_options.fft_N*_options.freq_step_division*_options.nb_f_bins), transpose_index_C(_options.nb_batch_prns, prn_position)))
            )
        ),
        cmulc_functor()
    );

    cufftHandle ifft_plan;

	int n[1];                                       //!< CUFFT Plan FFT size parameter
	int inembed[1];                                 //!< CUFFT Plan parameter
	int onembed[1];                                 //!< CUFFT Plan parameter

    n[0] = _options.fft_N;
    inembed[0] = _options.fft_N;
    onembed[0] = _options.fft_N;

    cufftResult_t fft_stat = cufftPlanMany(&ifft_plan, 1, n,
		inembed, 2*_options.nb_batch_prns, 2*_options.nb_batch_prns*_options.fft_N,
		onembed, 2*_options.nb_batch_prns, 2*_options.nb_batch_prns*_options.fft_N,
		CUFFT_C2C, _options.nb_f_bins*_options.freq_step_division);

    if (fft_stat != CUFFT_SUCCESS)
    {
    	std::ostringstream err_os;
    	err_os << "CUFFT Error: Unable to create plan for pilot IFFT RC=" << fft_stat;
    	throw WsgcException(err_os.str());
    }

    if (cufftExecC2C(ifft_plan,
    		         thrust::raw_pointer_cast(&d_ifft_in[prn_position]),
    		         thrust::raw_pointer_cast(&d_ifft_out[prn_position]),
    		         CUFFT_INVERSE) != CUFFT_SUCCESS)
    {
    	throw WsgcException("CUFFT Error: Failed to execute IFFT");
    }

    thrust::copy(d_ifft_out.begin(), d_ifft_out.end(), h_ifft_out.begin());

    std::cout << "--- IFFT result :" << std::endl;

	unsigned int bi;
	unsigned int ffti;
	unsigned int fsi;
	unsigned int fhi;

    for (unsigned int i=0; i < _options.fft_N*_options.nb_f_bins*_options.freq_step_division*2*_options.nb_batch_prns; i++)
    {
    	decomp_full_index(i, bi, ffti, fsi, fhi);
    	std::cout << i << ":" << bi << ":" << ffti << ":" << fsi << ":" << fhi << ": (" << h_ifft_out[i].x << "," << h_ifft_out[i].y << ")" << std::endl;
    }

    std::cout << "--- straight:" << std::endl;

    float max_mag = thrust::transform_reduce(d_ifft_out.begin(), d_ifft_out.end(), mag_squared_functor<cuComplex, float>(), 0.0, thrust::maximum<float>());
    thrust::device_vector<cuComplex>::iterator max_element = thrust::max_element(d_ifft_out.begin(), d_ifft_out.end(), lesser_mag_squared<cuComplex>());
    cuComplex z = *max_element;

    std::cout << max_mag << std::endl;
    unsigned int max_full_index = max_element - d_ifft_out.begin();
    decomp_full_index(max_full_index, bi, ffti, fsi, fhi);
	std::cout << max_full_index << ":" << bi << ":" << ffti << ":" << fsi << ":" << fhi << ": (" << z.x << "," << z.y << ") " << mag_squared_functor<cuComplex, float>()(z) << std::endl;

	std::cout << "--- gay stride:" << std::endl;

	strided_range<thrust::device_vector<cuComplex>::iterator> strided_ifft_out(d_ifft_out.begin()+prn_position, d_ifft_out.end(), 2*_options.nb_batch_prns);

	strided_range<thrust::device_vector<cuComplex>::iterator>::iterator strided_max_element_it = thrust::max_element(strided_ifft_out.begin(), strided_ifft_out.end(), lesser_mag_squared<cuComplex>());
	z = *strided_max_element_it;
	unsigned int max_strided_index = strided_max_element_it - strided_ifft_out.begin();
	decomp_strided_index(max_strided_index,ffti, fsi, fhi);
	std::cout << max_full_index << ":" << ffti << ":" << fsi << ":" << fhi << ": (" << z.x << "," << z.y << ") " << mag_squared_functor<cuComplex, float>()(z) << std::endl;


    cufftDestroy(ifft_plan);
}


void test_cuda::decomp_full_index(unsigned int full_index, unsigned int& bi, unsigned int& ffti, unsigned int& fsi, unsigned int& fhi)
{
	bi = full_index % (2*_options.nb_batch_prns);
	ffti = (full_index / (2*_options.nb_batch_prns)) % _options.fft_N;
	unsigned int fi = (full_index / (2*_options.nb_batch_prns)) / _options.fft_N;
	fsi = fi % _options.freq_step_division;
	fhi = fi / _options.freq_step_division;
}

void test_cuda::decomp_strided_index(unsigned int strided_index, unsigned int& ffti, unsigned int& fsi, unsigned int& fhi)
{
	ffti = strided_index % _options.fft_N;
	unsigned int fi = strided_index / _options.fft_N;
	fsi = fi % _options.freq_step_division;
	fhi = fi / _options.freq_step_division;
}
