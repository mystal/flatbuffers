// Copyright 2015 Sam Payson. All rights reserved.
// 
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
// 
//     http://www.apache.org/licenses/LICENSE-2.0
// 
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

// TODO: Remove this when the module is finished.
#![allow(dead_code)]

extern crate num;

use std::cmp;
use std::marker;
use std::mem;
use std::slice;
use std::str;

pub type UOffset = u32;

pub type SOffset = i32;

pub type VOffset = u16;

pub trait Endian: Copy {
    unsafe fn read_le(buf: *const u8) -> Self;
    unsafe fn write_le(self, buf: *mut u8);
}

// What we reall want here is:
//
//     impl<T: num::PrimInt> Endian for T {
//         fn read_le(buf: &[u8]) -> T {
//             let ptr: &T = unsafe { mem::transmute(&buf[0]) };
//             num::PrimInt::from_le(*ptr)
//         }
//     
//         fn write_le(self, buf: &mut [u8]) {
//             let ptr: &mut T = unsafe { mem::transmute(&mut buf[0]) };
//             *ptr = self.to_le();
//         }
//     }
//
// but the blanket impl causes errors if we try to implement it for any other type, so this macro
// will have to do.
macro_rules! impl_endian_for {
    ($t:ty) => {
        impl Endian for $t {
            unsafe fn read_le(buf: *const u8) -> $t {
                let ptr: &$t = mem::transmute(buf);
                num::PrimInt::from_le(*ptr)
            }

            unsafe fn write_le(self, buf: *mut u8) {
                let ptr: &mut $t = mem::transmute(buf);
                *ptr = self.to_le();
            }
        }
    }
}

impl_endian_for!(u8);
impl_endian_for!(i8);
impl_endian_for!(u16);
impl_endian_for!(i16);
impl_endian_for!(u32);
impl_endian_for!(i32);
impl_endian_for!(u64);
impl_endian_for!(i64);
impl_endian_for!(usize);
impl_endian_for!(isize);

unsafe fn index<T>(base: *const u8, idx: usize) -> *const u8 {
    let base_us: usize = mem::transmute(base);

    mem::transmute(base_us + idx * mem::size_of::<T>())
}

unsafe fn index_mut<T>(base: *mut u8, idx: usize) -> *mut u8 {
    let base_us: usize = mem::transmute(base);

    mem::transmute(base_us + idx * mem::size_of::<T>())
}

unsafe fn offset(base: *const u8, off: usize) -> *const u8 {
    let base_us: usize = mem::transmute(base);

    mem::transmute(base_us + off)
}

unsafe fn offset_mut(base: *mut u8, off: usize) -> *mut u8 {
    let base_us: usize = mem::transmute(base);

    mem::transmute(base_us + off)
}

unsafe fn soffset(base: *const u8, off: isize) -> *const u8 {
    let base_is: isize = mem::transmute(base);

    mem::transmute(base_is + off)
}

unsafe fn soffset_mut(base: *mut u8, off: isize) -> *mut u8 {
    let base_is: isize = mem::transmute(base);

    mem::transmute(base_is + off)
}

/// This implementation assumes that the endianness of the FPU is the same as for integers.
impl Endian for f32 {
    unsafe fn read_le(buf: *const u8) -> f32 {
        let ptr: &u32 = mem::transmute(buf);
        mem::transmute(num::PrimInt::from_le(*ptr))
    }

    unsafe fn write_le(self, buf: *mut u8) {
        let ptr: &mut u32 = mem::transmute(buf);
        *ptr = mem::transmute(self);
    }
}

/// This implementation assumes that the endianness of the FPU is the same as for integers.
impl Endian for f64 {
    unsafe fn read_le(buf: *const u8) -> f64 {
        let ptr: &u64 = mem::transmute(buf);
        mem::transmute(num::PrimInt::from_le(*ptr))
    }

    unsafe fn write_le(self, buf: *mut u8) {
        let ptr: &mut u64 = mem::transmute(buf);
        *ptr = mem::transmute(self);
    }
}

unsafe fn read_scalar<T: Endian>(buf: *const u8) -> T {
    Endian::read_le(buf)
}

unsafe fn write_scalar<T: Endian>(buf: *mut u8, val: T) {
    val.write_le(buf)
}

pub trait Indirect<I> {
    unsafe fn read(buf: *const u8, idx: usize) -> I;
}

impl<T: Copy> Indirect<T> for T {
    unsafe fn read(buf: *const u8, idx: usize) -> T {
        let off = idx * mem::size_of::<T>();
        let ptr: &T = mem::transmute(offset(buf, off));

        *ptr
    }
}

pub struct Offset<T> {
    inner: UOffset,
    _t:    marker::PhantomData<T>,
}

impl<'x, T> Indirect<&'x T> for Offset<T> {
    unsafe fn read(buf: *const u8, idx: usize) -> &'x T {
        let off: UOffset = read_scalar(index::<UOffset>(buf, idx));
        mem::transmute(offset(buf, off as usize))
    }
}

/// A helper type for accessing vectors in flatbuffers.
pub struct Vector<T, I = T> where T: Indirect<I> {
    length: UOffset,
    _t:     marker::PhantomData<T>,
    _i:     marker::PhantomData<I>,
}

pub struct VecIter<'x, I: 'x, T: Indirect<I> + 'x> {
    vec: &'x Vector<T, I>,
    idx: usize,
}

impl<'x, I, T: Indirect<I>> Iterator for VecIter<'x, I, T> {
    type Item = I;

    fn next(&mut self) -> Option<I> {
        let idx = self.idx;
        self.idx = idx + 1;

        self.vec.get(idx)
    }
}

impl<I, T: Indirect<I>> Vector<T, I> {
    unsafe fn data(&self) -> *const u8 {
        index::<UOffset>(mem::transmute(self), 1)
    }

    pub fn len(&self) -> usize {
        self.length as usize
    }

    pub fn get(&self, idx: usize) -> Option<I> {
        if idx < self.len() {
            Some(unsafe { <T as Indirect<I>>::read(self.data(), idx) })
        } else {
            None
        }
    }
}

pub type String = Vector<i8>;

impl AsRef<str> for String {
    fn as_ref(&self) -> &str {
        let slc = unsafe {
            let ptr = self.data();
            let len = self.len();

            slice::from_raw_parts(ptr, len)
        };

        // TODO: Should this be the checked version? If so, do we want to panic if it's not utf-8?
        //
        //       This (unchecked) version certainly reflects the performance characteristics in the
        //       spirit of the format. Maybe the `AsRef<str>` implementation should be checked, and
        //       there should be an unsafe fast method?
        //
        //       I'll think about it later...
        unsafe { str::from_utf8_unchecked(slc) }
    }
}

impl PartialEq for String {
    fn eq(&self, other: &String) -> bool {
        let (a, b): (&str, &str) = (self.as_ref(), other.as_ref());
        a.eq(b)
    }
}

impl PartialOrd for String {
    fn partial_cmp(&self, other: &String) -> Option<cmp::Ordering> {
        let (a, b): (&str, &str) = (self.as_ref(), other.as_ref());
        a.partial_cmp(b)
    }
}

impl Eq for String {}

impl Ord for String {
    fn cmp(&self, other: &String) -> cmp::Ordering {
        let (a, b): (&str, &str) = (self.as_ref(), other.as_ref());
        a.cmp(b)
    }
}

pub struct Table;

impl Table {
    fn get_optional_field_offset(&self, field: VOffset) -> Option<VOffset> {
        unsafe {
            let base = mem::transmute(self);

            // I'm not suire why it's subtraction, instead of addition, but this is what they have in
            // the C++ code.
            let vtable = soffset(base, -read_scalar::<SOffset>(base) as isize);

            let vtsize: VOffset = read_scalar(vtable);

            if field < vtsize {
                let voff = read_scalar(index::<VOffset>(vtable, field as usize));
                if voff != 0 {
                    return Some(voff)
                }
            }

            None
        }
    }

    pub fn get_field<T: Endian>(&self, field: VOffset, def: T) -> T {

        self.get_optional_field_offset(field)
            .map_or(def, |voffs| unsafe {
                let base = mem::transmute(self);
                read_scalar(offset(base, voffs as usize))
            } )
    }

    pub fn get_ref<T>(&self, field: VOffset) -> Option<&T> {
        self.get_optional_field_offset(field)
            .map(|voffs| unsafe {
                let base = mem::transmute(self);
                let p    = offset(base, voffs as usize);
                let offs: UOffset = read_scalar(p);
                mem::transmute(offset(p, offs as usize))
            } )
    }

    pub fn get_ref_mut<T>(&mut self, field: VOffset) -> Option<&mut T> {
        self.get_optional_field_offset(field)
            .map(|voffs| unsafe {
                let base = mem::transmute(self);
                let p    = offset_mut(base, voffs as usize);
                let offs: UOffset = read_scalar(p);
                mem::transmute(offset_mut(p, offs as usize))
            } )
    }

    pub fn get_struct<T>(&self, field: VOffset) -> Option<&T> {
        self.get_optional_field_offset(field)
            .map(|voffs| unsafe {
                let base = mem::transmute(self);
                mem::transmute(offset(base, voffs as usize))
            } )
    }

    pub fn get_struct_mut<T>(&mut self, field: VOffset) -> Option<&mut T> {
        self.get_optional_field_offset(field)
            .map(|voffs| unsafe {
                let base = mem::transmute(self);
                mem::transmute(offset_mut(base, voffs as usize))
            } )
    }

    pub fn set_field<T: Endian>(&mut self, field: VOffset, val: T) {
        unsafe {
            // We `unwrap` here because the caller is expected to verify that the field exists
            // beforehand by calling `check_field`.
            let voffs = self.get_optional_field_offset(field).unwrap();

            let base = mem::transmute(self);

            write_scalar(offset_mut(base, voffs as usize), val);
        }
    }

    pub fn check_field(&self, field: VOffset) -> bool {
        self.get_optional_field_offset(field).is_some()
    }
}

pub struct Struct;

impl Struct {
    pub fn get_field<T: Endian>(&self, off: UOffset) -> T {
        unsafe {
            let base = mem::transmute(self);
            read_scalar(offset(base, off as usize))
        }
    }

    pub fn get_ref<T>(&self, off: UOffset) -> &T {
        unsafe {
            let base = mem::transmute(self);
            let p    = offset(base, off as usize);

            mem::transmute(offset(p, read_scalar::<UOffset>(p) as usize))
        }
    }

    pub fn get_ref_mut<T>(&self, off: UOffset) -> &T {
        unsafe {
            let base = mem::transmute(self);
            let p    = offset_mut(base, off as usize);

            mem::transmute(offset_mut(p, read_scalar::<UOffset>(p) as usize))
        }
    }

    pub fn get_struct<T>(&self, off: UOffset) -> &T {
        unsafe {
            let base = mem::transmute(self);

            mem::transmute(offset(base, off as usize))
        }
    }

    pub fn get_struct_mut<T>(&mut self, off: UOffset) -> &T {
        unsafe {
            let base = mem::transmute(self);

            mem::transmute(offset_mut(base, off as usize))
        }
    }
}
