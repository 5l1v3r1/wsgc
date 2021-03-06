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
      
     Decision box

     Final correlation data analysis and message estimation 
*/
#include "WsgcUtils.h"
#include "DecisionBox.h"
#include "DecisionBox_Thresholds.h"
#include <iostream>
#include <sstream>
#include <iomanip>
#include <algorithm>

const unsigned int DecisionBox::preferred_symbol_prn_i_margin_threshold = 2;
const unsigned int DecisionBox::single_preferred_symbol_prn_i_threshold = 4;


//=================================================================================================
DecisionBox::DecisionBox(unsigned int prn_per_symbol, unsigned int _fft_size, const DecisionBox_Thresholds& decision_thresholds) :
	_prn_per_symbol(prn_per_symbol),
	_fft_size(_fft_size),
	_preferred_symbol_prn_i(0),
	_mag_display_adj_factor(1.0),
	_prni_at_max_invalid(true),
	_use_cuda(false),
    _decision_thresholds(decision_thresholds)
{}


//=================================================================================================
DecisionBox::~DecisionBox()
{}


//=================================================================================================
void DecisionBox::estimate_preferred_symbol_prn_i(const std::vector<CorrelationRecord>& correlation_records)
{
    // for each PRN index in symbol compute histogram of preferred shift. The preferred PRN index is the one showing the best singular value moreover if it corresponds to global preferred shift.
    std::vector<CorrelationRecord>::const_iterator record_it = correlation_records.begin();
    const std::vector<CorrelationRecord>::const_iterator records_end = correlation_records.end();
    wsgc_float prev_max = 0.0;
    wsgc_float ante_prev_max = 0.0;
    unsigned int prn_per_symbol_i = 0;
    unsigned int prev_prn_per_symbol_i;
    std::map<unsigned int, unsigned int>::iterator prn_i_occurences_it;
    std::map<unsigned int, unsigned int>::const_iterator shift_occurences_it;
    
    if (correlation_records.size() > std::max(_prn_per_symbol,2u))
    {
        // find preferred PRN per symbol index (symbol synchronization)
        for (; record_it != records_end; ++record_it)
        {
            if (record_it > correlation_records.begin()+1) // not first nor second time round
            {
                if ((ante_prev_max < prev_max) && (prev_max > record_it->magnitude_max)) // local maximum at previous index
                {
                    //std::cout << "local max detected at index " << prev_prn_per_symbol_i << " : " << ante_prev_max << "<" << prev_max << ">" << record_it->magnitude_max << std::endl;
                    prn_i_occurences_it = _symbol_prn_i_at_max.find(prev_prn_per_symbol_i);
                    if (prn_i_occurences_it == _symbol_prn_i_at_max.end())
                    {
                        _symbol_prn_i_at_max[prev_prn_per_symbol_i] = 1;
                    }
                    else
                    {
                        _symbol_prn_i_at_max[prev_prn_per_symbol_i] += 1;
                    }
                }
            }
            
            ante_prev_max = prev_max;
            prev_max = record_it->magnitude_max;
            prev_prn_per_symbol_i = prn_per_symbol_i;
            
            if (prn_per_symbol_i < _prn_per_symbol - 1)
            {
                prn_per_symbol_i++;
            }
            else
            {
                prn_per_symbol_i = 0;
            }
        }
        
        // reverse maps to get histogram data
        for (prn_i_occurences_it = _symbol_prn_i_at_max.begin(); prn_i_occurences_it != _symbol_prn_i_at_max.end(); ++prn_i_occurences_it)
        {
            _histo_symbol_prn_i_at_max.push_back(*prn_i_occurences_it);
        }
        
        std::sort(_histo_symbol_prn_i_at_max.begin(), _histo_symbol_prn_i_at_max.end(), GreaterPrnIndex(_prn_per_symbol));
        
        std::ostringstream prni_os;
        WsgcUtils::print_histo(_histo_symbol_prn_i_at_max, prni_os);
        
        std::cout << "Max at symbol PRNi .................: " << prni_os.str() << std::endl;
    }
    else
    {
        std::cout << "DecisionBox: not enough correlation records to analyze symbol synchronization" << std::endl;
    }
    
    if (_histo_symbol_prn_i_at_max.size() == 0)
    {
    	std::cout << "No occurrences of best prn index in sequence : message should be invalidated" << std::endl;
    	_prni_at_max_invalid = true;
    }
    else if ((_histo_symbol_prn_i_at_max.size() == 1) && (_histo_symbol_prn_i_at_max.begin()->second < single_preferred_symbol_prn_i_threshold))
    {
    	std::cout << "Number of occurrences for single best prn index in sequence is too small (<" << single_preferred_symbol_prn_i_threshold << "): message should be invalidated" << std::endl;
        _preferred_symbol_prn_i = _histo_symbol_prn_i_at_max.begin()->first; // take best
    	_prni_at_max_invalid = true;
    }
    else if ((((_histo_symbol_prn_i_at_max.begin()->second - (_histo_symbol_prn_i_at_max.begin()+1)->second)) < preferred_symbol_prn_i_margin_threshold))
    {
        std::cout << "Best and second best prn index in sequence at peak margin is too small (<" << preferred_symbol_prn_i_margin_threshold << "): message should be invalidated" << std::endl;
        _preferred_symbol_prn_i = _histo_symbol_prn_i_at_max.begin()->first; // take best
        _prni_at_max_invalid = true;
    }
    /*
    // best and second best hits difference is less than 3 
    if ((((_histo_symbol_prn_i_at_max.begin()->second - (_histo_symbol_prn_i_at_max.begin()+1)->second)) < preferred_symbol_prn_i_margin_threshold))
    {
        std::cout << "Best and second best prn index in sequence at peak margin is too small (<" << preferred_symbol_prn_i_margin_threshold << "): message should be invalidated" << std::endl;
        _prni_at_max_invalid = true;
        // best and second best are next in sequence
        if (WsgcUtils::shortest_cyclic_distance(_histo_symbol_prn_i_at_max.begin()->first, (_histo_symbol_prn_i_at_max.begin()+1)->first, _prn_per_symbol) == 1) 
        {
            // if best is earlier than second best in cyclic sequence then take second best 
            if ((_histo_symbol_prn_i_at_max.begin()->first == 0) && ((_histo_symbol_prn_i_at_max.begin()+1)->first == _prn_per_symbol-1))
            {
                _preferred_symbol_prn_i = _histo_symbol_prn_i_at_max.begin()->first; // take best
            }
            else if ((_histo_symbol_prn_i_at_max.begin()->first == _prn_per_symbol-1) && ((_histo_symbol_prn_i_at_max.begin()+1)->first == 0))
            {
                _preferred_symbol_prn_i = (_histo_symbol_prn_i_at_max.begin()+1)->first; // take second best
            }
            else if (_histo_symbol_prn_i_at_max.begin()->first < (_histo_symbol_prn_i_at_max.begin()+1)->first)
            { 
                _preferred_symbol_prn_i = (_histo_symbol_prn_i_at_max.begin()+1)->first; // take second best
            }
            // else take best
            else
            {
                _preferred_symbol_prn_i = _histo_symbol_prn_i_at_max.begin()->first; // take best
            }
        }
        // if best and second best are far apart in cyclic sequence then a good decision cannot be made
        else
        {
            std::cout << "Broad preferred symbol PRN indexes: message should be invalidated" << std::endl;
            _preferred_symbol_prn_i = _histo_symbol_prn_i_at_max.begin()->first; // take best
        }
    }
    */
    // best has a number of hits large enough above the rest so take it
    else
    {
        _preferred_symbol_prn_i = _histo_symbol_prn_i_at_max.begin()->first; // take best
        _prni_at_max_invalid = false;
    }

    std::cout << "Preferred symbol PRN index is " << _preferred_symbol_prn_i << std::endl;
}


//=================================================================================================
bool DecisionBox::histo_order(const std::pair<unsigned int, unsigned int>& i, const std::pair<unsigned int, unsigned int>& j) 
{
    return j.second < i.second;
}


//=================================================================================================
DecisionRecord& DecisionBox::new_decision_record()
{
    static const DecisionRecord tmp_decision_record;
    _decision_records.push_back(tmp_decision_record);

    return _decision_records.back();
}


//=================================================================================================
void DecisionBox::dump_decision_records(std::ostringstream& os) const
{
    std::vector<DecisionRecord>::const_iterator it = _decision_records.begin();
    const std::vector<DecisionRecord>::const_iterator it_end = _decision_records.end();
    
    DecisionRecord::dump_banner(os);
    
    for (; it != it_end; ++it)
    {
        it->dump_line(os, "_DCR", _mag_display_adj_factor);
    }
}


//=================================================================================================
void DecisionBox::dump_decision_status(std::ostringstream& os, std::vector<unsigned int>& original_symbols, bool no_trivial) const
{
    unsigned int count_false_reject = 0;
    unsigned int count_false_accept = 0;
    unsigned int count_true_reject = 0;
    unsigned int count_true_accept = 0;

	os << "St ";
	DecisionRecord::dump_banner(os);

	for (unsigned int i=0; i < original_symbols.size(); i++)
	{
		if (i < _decision_records.size())
		{
			decision_status_t decision_status;
			bool print = true;

			if ((_decision_records[i].select_count > 0) && (original_symbols[i] == _decision_records[i].prn_index_max))
			{
				if (_decision_records[i].validated)
				{
					decision_status = decision_status_true_accept;
                    count_true_accept++;                    

					if (_decision_records[i].decision_type == DecisionRecord::decision_ok_strong)
					{
						print = false;
					}
				}
				else
				{
					decision_status = decision_status_false_reject;
                    count_false_reject++;
				}
			}
			else
			{
				if (_decision_records[i].validated)
				{
					decision_status = decision_status_false_accept;
                    count_false_accept++;
				}
				else
				{
					decision_status = decision_status_true_reject;
                    count_true_reject++;

					if (_decision_records[i].decision_type == DecisionRecord::decision_ko_no_valid_rec)
					{
						print = false;
					}
				}
			}

			if (!(no_trivial) || print)
			{
				dump_decoding_status(os, decision_status);
				os << " ";
				_decision_records[i].dump_line(os, "_DCS", _mag_display_adj_factor);
			}
		}
	}
    
    unsigned int count_erasures = count_true_reject + count_false_reject;
    unsigned int count_errors = count_false_accept;
    unsigned int rs_corr = count_erasures + 2*count_errors; // Number of symbols that need to be corrected by Reed Solomon to get clean decode (number of erasures plus two times the number of errors)
    os << "_SDC " << count_true_accept << "," << count_true_reject << "," << count_false_accept << "," << count_false_reject << "," << rs_corr << std::endl;
}


//=================================================================================================
void DecisionBox::dump_decoding_status(std::ostringstream& os, decision_status_t decision_status) const
{
	switch (decision_status)
	{
	case decision_status_false_accept:
		os << "FA";
		break;
	case decision_status_false_reject:
		os << "FR";
		break;
	case decision_status_true_accept:
		os << "TA";
		break;
	case decision_status_true_reject:
		os << "TR";
		break;
	default:
		os << "XX";
		break;
	}
}
