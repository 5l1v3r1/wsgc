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

     Cuda Device Manager

	 All CUDA implementing classes should inherit of this class. It makes sure
	 it is initialized to the specified CUDA device number

*/
#ifndef __CUDA_DEVICE_MANAGER_H__
#define __CUDA_DEVICE_MANAGER_H__

/**
 * \brief All CUDA implementing classes should inherit of this class. It makes sure
 *        it is initialized to the specified CUDA device number
 */
class CudaDeviceManager
{
public:
	CudaDeviceManager(unsigned int cuda_device);
	virtual ~CudaDeviceManager();

	unsigned int get_cuda_device() const
	{
		return _cuda_device;
	}

protected:
	unsigned int _cuda_device; //!< CUDA GPU# on which to run
};

#endif // __CUDA_DEVICE_MANAGER_H__
