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

     LocalCodes

     Creates a local copy of all symbols gold codes in the time domain 
     i.e. complex cojugate of the code modulated samples
     Used for plain time domain correlation

*/

#ifndef __LOCAL_CODES_H__
#define __LOCAL_CODES_H__

#include "WsgcTypes.h"
#include <vector>

class CodeModulator;
class GoldCodeGenerator;


/**
 * \brief Local copy of codes to be used for time domain correlation
 *
 * Creates a local copy of all symbols gold codes
 *
 */
class LocalCodes
{
public:
    /**
    * \param code_modulator Modulator used to build the codes
    * \param gc_generator Gold Code generator used to build the codes
    * \param f_sampling Sampling frequency
    * \param f_chip Chip rate
	* \param symbols List of symbols to be processed
    */
	LocalCodes(
            CodeModulator& code_modulator, 
            const GoldCodeGenerator& gc_generator, 
            wsgc_float f_sampling, 
            wsgc_float f_chip,
            std::vector<unsigned int>& symbols);

	virtual ~LocalCodes();
    
    /**
     * Get Gold Code generator reference
     */
    const GoldCodeGenerator& get_gc_generator() const
    {
    	return _gc_generator;
    }

	/**
	 * Get the number of samples per code (corresponds to FFT length)
	 * \return Pointer to the first element of the local copy of the code
	 */
	unsigned int get_nb_code_samples() const
	{
		return _nb_code_samples;
	}

	/**
	 * Get the number of codes
	 * \return The number of stored PNR codes
	 */
	unsigned int get_nb_codes() const
	{
		return _symbols.size();
	}

	/**
	 * Get the vector of PRN symbols
	 * \return The vector of PRN symbols
	 */
	const std::vector<unsigned int>& get_prns() const
	{
		return _symbols;
	}

protected:
    CodeModulator& _code_modulator; //!< Reference to the code modulator
    const GoldCodeGenerator& _gc_generator; //!< Reference to the Gold Code generator
    wsgc_float _f_sampling; //!< Sampling frequency
    wsgc_float _f_chip; //!< Chip rate
    std::vector<unsigned int>& _symbols; //!< List of symbols to be processed
	unsigned int _nb_code_samples; //!< Number of samples in one code length
};

#endif /* __LOCAL_CODES_H__ */
