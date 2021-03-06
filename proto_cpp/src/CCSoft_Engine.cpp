/*
     Copyright 2012-2013 Edouard Griffiths <f4exb at free dot fr>

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

     CCSoft_Engine

     Convolutional coding soft decision engine based on ccsoft library
     https://code.google.com/p/rssoft/#Convolutional_codes_library

*/

#include "CCSoft_Engine.h"
#include "CCSoft_Exception.h"
#include "CC_StackDecoding_FA.h"
#include "CC_FanoDecoding_FA.h"
#include "SourceCodec.h"

#include <cmath>
#include <regex>

//=================================================================================================
CCSoft_Engine::CCSoft_Engine(const std::vector<unsigned int>& _k_constraints,
        const std::vector<std::vector<unsigned int> >& _generator_polys) :
            k_constraints(_k_constraints),
            generator_polys(_generator_polys),
            k(_k_constraints.size()),
            algorithm_type(CCSoft_Engine::Algorithm_Fano),
            cc_decoding(0),
            fano_init_metric(0.0),
            fano_delta_metric(1.0),
            fano_tree_cache_size(0),
            init_edge_bias(0.0),
            edge_bias(0.0),
            max_nb_retries(1),
            index_retries(0),
            edge_bias_decrement(0.0),
            verbosity(0)
{}


//=================================================================================================
CCSoft_Engine::~CCSoft_Engine()
{
    if (cc_decoding)
    {
        delete cc_decoding;
    }
}


//=================================================================================================
void CCSoft_Engine::init_decoding_stack(float _edge_bias)
{
    init_edge_bias = _edge_bias;
    edge_bias = _edge_bias;
    cc_decoding = new ccsoft::CC_StackDecoding_FA<unsigned int, unsigned int, 1>(k_constraints, generator_polys);
    cc_decoding->set_edge_bias(edge_bias);
}

//=================================================================================================
void CCSoft_Engine::init_decoding_fano(float _edge_bias,
        float init_threshold,
        float delta_threshold,
        unsigned int tree_cache_size,
        float init_threshold_delta)
{
	init_edge_bias = _edge_bias;
	edge_bias = _edge_bias;
    cc_decoding = new ccsoft::CC_FanoDecoding_FA<unsigned int, unsigned int, 1>(k_constraints,
            generator_polys,
            init_threshold,
            delta_threshold,
            tree_cache_size,
            init_threshold_delta);
    cc_decoding->set_edge_bias(edge_bias);
}

//=================================================================================================
void CCSoft_Engine::print_convolutional_code_data(std::ostream& os)
{
    cc_decoding->get_encoding().print(os);
}

//=================================================================================================
void CCSoft_Engine::print_stats(std::ostream& os, bool decode_ok)
{
    cc_decoding->print_stats(os, decode_ok);
    os << " #retries = " << index_retries+1 << " edge_bias = " << edge_bias << std::endl;
    cc_decoding->print_stats_summary(os, decode_ok);
    os << "," << index_retries+1 << "," << edge_bias << std::endl;
}

//=================================================================================================
void CCSoft_Engine::encode(const std::vector<unsigned int>& in_msg, std::vector<unsigned int>& out_codeword)
{
    std::vector<unsigned int>::const_iterator in_it = in_msg.begin();
    out_codeword.clear();

    for (; in_it != in_msg.end(); ++in_it)
    {
        out_codeword.push_back(0);
        cc_decoding->get_encoding().encode(*in_it, out_codeword.back());
    }
}

//=================================================================================================
void CCSoft_Engine::reset()
{
    if (cc_decoding)
    {
        cc_decoding->reset();
    }
}

//=================================================================================================
bool CCSoft_Engine::decode(ccsoft::CC_ReliabilityMatrix& relmat, std::vector<unsigned int>& retrieved_msg, float& score)
{
    bool success = false;
    relmat.normalize();

    if (cc_decoding)
    {
        success = cc_decoding->decode(relmat, retrieved_msg);
        score = cc_decoding->get_score();

        if (verbosity > 0)
        {
            if (success)
            {
                std::cout << "decoding successful with retrieved message score of " << score << std::endl;
            }
            else
            {
                std::cout << "decoding unsuccessful" << std::endl;
            }
        }
    }

    return success;
}

//=================================================================================================
bool CCSoft_Engine::decode_regex(ccsoft::CC_ReliabilityMatrix& relmat,
		std::vector<unsigned int>& retrieved_msg,
		float& score,
		std::string& retrieved_text_msg,
		const SourceCodec& src_codec,
		const std::string& regexp)
{
    bool success = false;
    relmat.normalize();

    if (cc_decoding)
    {
        for (index_retries=0; index_retries<max_nb_retries; index_retries++)
        {
        	edge_bias = init_edge_bias - index_retries*edge_bias_decrement;
            std::cout << "Try #" << index_retries+1 << " " << edge_bias << std::endl;
            cc_decoding->set_edge_bias(edge_bias);
        	success = cc_decoding->decode(relmat, retrieved_msg);
        	score = cc_decoding->get_score();

        	if (success)
        	{
        		src_codec.decode(retrieved_msg, retrieved_text_msg);

        		if (regexp_match(retrieved_text_msg,regexp))
        		{
        			break;
        		}
        	}
        }
    }

    return success;
}

//=================================================================================================
bool CCSoft_Engine::decode_match_msg(ccsoft::CC_ReliabilityMatrix& relmat,
		std::vector<unsigned int>& retrieved_msg,
		float& score,
		const std::vector<unsigned int>& matching_msg)
{
    bool success = false;
    relmat.normalize();

    if (cc_decoding)
    {
        for (index_retries=0; index_retries<max_nb_retries; index_retries++)
        {
        	edge_bias = init_edge_bias - index_retries*edge_bias_decrement;
            std::cout << "Try #" << index_retries+1 << " " << edge_bias << std::endl;
            cc_decoding->set_edge_bias(edge_bias);
        	success = cc_decoding->decode(relmat, retrieved_msg);
        	score = cc_decoding->get_score();

        	if (success)
        	{
        		if (retrieved_msg == matching_msg)
        		{
        			break;
        		}
        	}
        }
    }

    return success;
}

//=================================================================================================
bool CCSoft_Engine::decode_match_str(ccsoft::CC_ReliabilityMatrix& relmat,
		std::vector<unsigned int>& retrieved_msg,
		float& score,
		const SourceCodec& src_codec,
		const std::string& matching_str)
{
    bool success = false;
    relmat.normalize();
    std::string retrieved_text_msg;

    if (cc_decoding)
    {
        for (index_retries=0; index_retries<max_nb_retries; index_retries++)
        {
        	edge_bias = init_edge_bias - index_retries*edge_bias_decrement;
            std::cout << "Try #" << index_retries+1 << " " << edge_bias << std::endl;
            cc_decoding->set_edge_bias(edge_bias);
        	success = cc_decoding->decode(relmat, retrieved_msg);
        	score = cc_decoding->get_score();

        	if (success)
        	{
        		src_codec.decode(retrieved_msg, retrieved_text_msg);

        		if (retrieved_text_msg == matching_str)
        		{
        			break;
        		}
        	}
        }
    }

    return success;
}


//=================================================================================================
void CCSoft_Engine::interleave(std::vector<unsigned int>& symbols, bool forward)
{
	if (cc_decoding)
	{
		cc_decoding->interleave(symbols, forward);
	}
}

//=================================================================================================
bool CCSoft_Engine::regexp_match(const std::string& value,
		const std::string& regexp) const
{
	try
	{
		return std::regex_match(value, std::regex(regexp));
	}
	catch (std::regex_error& e)
	{
		std::cout << "Regular expression error: " << e.what() << " : " << e.code() << std::endl;
	}
	return false;
}


