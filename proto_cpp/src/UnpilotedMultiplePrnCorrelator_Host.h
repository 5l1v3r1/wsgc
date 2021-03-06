/*
     Copyright 2012 Edouard Griffiths <f4exb at free dot fr>

     This file is part of WSGC. A Weak Signal transmission mode using Gold Codes

     This program is free software; you can redistribute it and/or modify
     it under the terms of the GNU General Public License as published by
     the Free Software Foundation; either version 2 of the License, or
     (at your option) any later version.

     This program is distributed in the hope that it will be useful,
     but WITHOUT ANY WARRANTY; without even the implied warranty of
     MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
     GNU General Public License for more details.

     You should have received a copy of the GNU General Public License
     along with this program; if not, write to the Free Software
     Foundation, Inc., 51 Franklin Street, Boston, MA  02110-1301  USA

     Static not real time prototype in C++

     DifferentialModulationMultiplePrnCorrelator

     This flavour of correlator deals with PRNs without use of a pilot sequence
     This is the Host version

*/

#ifndef __UNPILOTED_MULTIPLE_PRN_CORRELATOR_HOST_H__
#define __UNPILOTED_MULTIPLE_PRN_CORRELATOR_HOST_H__

#include "UnpilotedMultiplePrnCorrelator.h"
#include "DemodulatorDifferential.h"
#include "LocalCodesFFT_Host.h"
#include <vector>
#include <cstring>

/**
 * \brief Correlator engine to do correlation of PRN(s) without use of a pilot sequence. Host version
 */
class UnpilotedMultiplePrnCorrelator_Host : public UnpilotedMultiplePrnCorrelator
{
public:
    /**
    * Correlator engine for message PRNs
    * \param f_sampling Sampling frequency
    * \param f_chip Chip rate (frequency)
    * \param prn_length Length of a PRN sequence in number of chips
    * \param prn_per_symbol Number of PRNs per message symbol
    * \param prn_list Reference to the vector of PRN numbers with which to make correlation
    * \param symbol_window_size Number of symbols used for processing. Storage is reserved for symbol_window_size times prn_per_symbol PRN samples
    * \param time_analysis_window_size Number of symbols used for time analysis.
    * \param correlation_records Reference to the correlation records
    * \param local_codes_fft_base Reference to the FFT copy of local codes for base modulation
    */
	UnpilotedMultiplePrnCorrelator_Host(
			wsgc_float f_sampling,
			wsgc_float f_chip,
			unsigned int prn_length,
            unsigned int prn_per_symbol,
			const std::vector<unsigned int>& prn_list,
			unsigned int symbol_window_size,
			unsigned int time_analysis_window_size,
			std::vector<CorrelationRecord>& correlation_records,
			const LocalCodesFFT_Host& local_codes_fft_base
        );
        
	virtual ~UnpilotedMultiplePrnCorrelator_Host();

	/**
	 * Do the message correlation over the PRN window
     * \param prn_per_symbol Number of PRNs per symbol
	 */
	virtual void execute();

	/**
	 * Append source samples for one PRN length to the buffer
     * \param samples Pointer to source samples
     * \return true if the buffer is complete and ready for analyse
	 */
	virtual bool set_samples(wsgc_complex *samples);

protected:
    const LocalCodesFFT_Host& _local_codes_fft_base; //!< Reference to the FFT copy of local codes for base modulation
    wsgc_complex *_samples; //!< Copy of input samples for the length of the window
    wsgc_complex *_src_fft; //!< Samples FFT
    wsgc_complex *_ifft_in_tmp; //!< temporary zone for IFFT input
    wsgc_complex *_ifft_out_tmp; //!< temporary zone for IFFT output
    wsgc_complex *_corr_out; //!< Correlation results
    wsgc_float *_corr_out_mag; //!< Correlation results magnitudes
    wsgc_fftw_plan _fft_plan; //!< FFTW plan for forward FFT.
    wsgc_fftw_plan _ifft_plan; //!< FFTW plan for inverse FFT.
    unsigned int _msg_time_analysis_symbols_count; //!< Count of symbols entered in the time analysis
    unsigned int _msg_time_analysis_corr_start_index; //!< Index in correlation records corresponding to the start of current analysis
    
    static const wsgc_complex _c_zero;

    /**
     * Do a whole correlation process over the PRNs window
     */
    void do_correlation();

    /**
     * Do correlation storing result at the chip sample shift place
     * \param prn_wi PRN index in window
     */
    void do_correlation(unsigned int prn_wi);

    /**
     * Do a sum averaging of correlations over the PRNs window
     * \param prn_wi PRN index in window
     */
    void do_sum_averaging();

    /**
     * Sum averaging of correlations over the PRNs window sub process with complex additions
     * \param prn_wi PRN index in window
     */
    void sum_averaging_loop_complex();

    /**
     * Sum averaging of correlations over the PRNs window sub process with magnitudes additions
     * \param prn_wi PRN index in window
     */
    void sum_averaging_loop_magnitudes();

};

#endif /* __UNPILOTED_MULTIPLE_PRN_CORRELATOR_HOST_H__ */
