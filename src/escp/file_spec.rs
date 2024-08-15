// automatically generated by the FlatBuffers compiler, do not modify


// @generated

use core::mem;
use core::cmp::Ordering;

extern crate flatbuffers;
use self::flatbuffers::{EndianScalar, Follow};

pub enum ESCP_file_listOffset {}
#[derive(Copy, Clone, PartialEq)]

pub struct ESCP_file_list<'a> {
  pub _tab: flatbuffers::Table<'a>,
}

impl<'a> flatbuffers::Follow<'a> for ESCP_file_list<'a> {
  type Inner = ESCP_file_list<'a>;
  #[inline]
  unsafe fn follow(buf: &'a [u8], loc: usize) -> Self::Inner {
    Self { _tab: flatbuffers::Table::new(buf, loc) }
  }
}

impl<'a> ESCP_file_list<'a> {
  pub const VT_ROOT: flatbuffers::VOffsetT = 4;
  pub const VT_FILES: flatbuffers::VOffsetT = 6;
  pub const VT_COMPLETE: flatbuffers::VOffsetT = 8;
  pub const VT_FC_STAT: flatbuffers::VOffsetT = 10;

  #[inline]
  pub unsafe fn init_from_table(table: flatbuffers::Table<'a>) -> Self {
    ESCP_file_list { _tab: table }
  }
  #[allow(unused_mut)]
  pub fn create<'bldr: 'args, 'args: 'mut_bldr, 'mut_bldr>(
    _fbb: &'mut_bldr mut flatbuffers::FlatBufferBuilder<'bldr>,
    args: &'args ESCP_file_listArgs<'args>
  ) -> flatbuffers::WIPOffset<ESCP_file_list<'bldr>> {
    let mut builder = ESCP_file_listBuilder::new(_fbb);
    if let Some(x) = args.files { builder.add_files(x); }
    if let Some(x) = args.root { builder.add_root(x); }
    builder.add_fc_stat(args.fc_stat);
    builder.add_complete(args.complete);
    builder.finish()
  }


  #[inline]
  pub fn root(&self) -> Option<&'a str> {
    // Safety:
    // Created from valid Table for this object
    // which contains a valid value in this slot
    unsafe { self._tab.get::<flatbuffers::ForwardsUOffset<&str>>(ESCP_file_list::VT_ROOT, None)}
  }
  #[inline]
  pub fn files(&self) -> Option<flatbuffers::Vector<'a, flatbuffers::ForwardsUOffset<File<'a>>>> {
    // Safety:
    // Created from valid Table for this object
    // which contains a valid value in this slot
    unsafe { self._tab.get::<flatbuffers::ForwardsUOffset<flatbuffers::Vector<'a, flatbuffers::ForwardsUOffset<File>>>>(ESCP_file_list::VT_FILES, None)}
  }
  #[inline]
  pub fn complete(&self) -> bool {
    // Safety:
    // Created from valid Table for this object
    // which contains a valid value in this slot
    unsafe { self._tab.get::<bool>(ESCP_file_list::VT_COMPLETE, Some(false)).unwrap()}
  }
  #[inline]
  pub fn fc_stat(&self) -> bool {
    // Safety:
    // Created from valid Table for this object
    // which contains a valid value in this slot
    unsafe { self._tab.get::<bool>(ESCP_file_list::VT_FC_STAT, Some(false)).unwrap()}
  }
}

impl flatbuffers::Verifiable for ESCP_file_list<'_> {
  #[inline]
  fn run_verifier(
    v: &mut flatbuffers::Verifier, pos: usize
  ) -> Result<(), flatbuffers::InvalidFlatbuffer> {
    use self::flatbuffers::Verifiable;
    v.visit_table(pos)?
     .visit_field::<flatbuffers::ForwardsUOffset<&str>>("root", Self::VT_ROOT, false)?
     .visit_field::<flatbuffers::ForwardsUOffset<flatbuffers::Vector<'_, flatbuffers::ForwardsUOffset<File>>>>("files", Self::VT_FILES, false)?
     .visit_field::<bool>("complete", Self::VT_COMPLETE, false)?
     .visit_field::<bool>("fc_stat", Self::VT_FC_STAT, false)?
     .finish();
    Ok(())
  }
}
pub struct ESCP_file_listArgs<'a> {
    pub root: Option<flatbuffers::WIPOffset<&'a str>>,
    pub files: Option<flatbuffers::WIPOffset<flatbuffers::Vector<'a, flatbuffers::ForwardsUOffset<File<'a>>>>>,
    pub complete: bool,
    pub fc_stat: bool,
}
impl<'a> Default for ESCP_file_listArgs<'a> {
  #[inline]
  fn default() -> Self {
    ESCP_file_listArgs {
      root: None,
      files: None,
      complete: false,
      fc_stat: false,
    }
  }
}

pub struct ESCP_file_listBuilder<'a: 'b, 'b> {
  fbb_: &'b mut flatbuffers::FlatBufferBuilder<'a>,
  start_: flatbuffers::WIPOffset<flatbuffers::TableUnfinishedWIPOffset>,
}
impl<'a: 'b, 'b> ESCP_file_listBuilder<'a, 'b> {
  #[inline]
  pub fn add_root(&mut self, root: flatbuffers::WIPOffset<&'b  str>) {
    self.fbb_.push_slot_always::<flatbuffers::WIPOffset<_>>(ESCP_file_list::VT_ROOT, root);
  }
  #[inline]
  pub fn add_files(&mut self, files: flatbuffers::WIPOffset<flatbuffers::Vector<'b , flatbuffers::ForwardsUOffset<File<'b >>>>) {
    self.fbb_.push_slot_always::<flatbuffers::WIPOffset<_>>(ESCP_file_list::VT_FILES, files);
  }
  #[inline]
  pub fn add_complete(&mut self, complete: bool) {
    self.fbb_.push_slot::<bool>(ESCP_file_list::VT_COMPLETE, complete, false);
  }
  #[inline]
  pub fn add_fc_stat(&mut self, fc_stat: bool) {
    self.fbb_.push_slot::<bool>(ESCP_file_list::VT_FC_STAT, fc_stat, false);
  }
  #[inline]
  pub fn new(_fbb: &'b mut flatbuffers::FlatBufferBuilder<'a>) -> ESCP_file_listBuilder<'a, 'b> {
    let start = _fbb.start_table();
    ESCP_file_listBuilder {
      fbb_: _fbb,
      start_: start,
    }
  }
  #[inline]
  pub fn finish(self) -> flatbuffers::WIPOffset<ESCP_file_list<'a>> {
    let o = self.fbb_.end_table(self.start_);
    flatbuffers::WIPOffset::new(o.value())
  }
}

impl core::fmt::Debug for ESCP_file_list<'_> {
  fn fmt(&self, f: &mut core::fmt::Formatter<'_>) -> core::fmt::Result {
    let mut ds = f.debug_struct("ESCP_file_list");
      ds.field("root", &self.root());
      ds.field("files", &self.files());
      ds.field("complete", &self.complete());
      ds.field("fc_stat", &self.fc_stat());
      ds.finish()
  }
}
pub enum FileOffset {}
#[derive(Copy, Clone, PartialEq)]

pub struct File<'a> {
  pub _tab: flatbuffers::Table<'a>,
}

impl<'a> flatbuffers::Follow<'a> for File<'a> {
  type Inner = File<'a>;
  #[inline]
  unsafe fn follow(buf: &'a [u8], loc: usize) -> Self::Inner {
    Self { _tab: flatbuffers::Table::new(buf, loc) }
  }
}

impl<'a> File<'a> {
  pub const VT_FINO: flatbuffers::VOffsetT = 4;
  pub const VT_NAME: flatbuffers::VOffsetT = 6;
  pub const VT_MODE: flatbuffers::VOffsetT = 8;
  pub const VT_UID: flatbuffers::VOffsetT = 10;
  pub const VT_GID: flatbuffers::VOffsetT = 12;
  pub const VT_SZ: flatbuffers::VOffsetT = 14;
  pub const VT_ATIM_SEC: flatbuffers::VOffsetT = 16;
  pub const VT_ATIM_NANO: flatbuffers::VOffsetT = 18;
  pub const VT_MTIM_SEC: flatbuffers::VOffsetT = 20;
  pub const VT_MTIM_NANO: flatbuffers::VOffsetT = 22;
  pub const VT_CRC: flatbuffers::VOffsetT = 24;
  pub const VT_COMPLETE: flatbuffers::VOffsetT = 26;

  #[inline]
  pub unsafe fn init_from_table(table: flatbuffers::Table<'a>) -> Self {
    File { _tab: table }
  }
  #[allow(unused_mut)]
  pub fn create<'bldr: 'args, 'args: 'mut_bldr, 'mut_bldr>(
    _fbb: &'mut_bldr mut flatbuffers::FlatBufferBuilder<'bldr>,
    args: &'args FileArgs<'args>
  ) -> flatbuffers::WIPOffset<File<'bldr>> {
    let mut builder = FileBuilder::new(_fbb);
    builder.add_mtim_nano(args.mtim_nano);
    builder.add_mtim_sec(args.mtim_sec);
    builder.add_atim_nano(args.atim_nano);
    builder.add_atim_sec(args.atim_sec);
    builder.add_sz(args.sz);
    builder.add_fino(args.fino);
    builder.add_complete(args.complete);
    builder.add_crc(args.crc);
    builder.add_gid(args.gid);
    builder.add_uid(args.uid);
    builder.add_mode(args.mode);
    if let Some(x) = args.name { builder.add_name(x); }
    builder.finish()
  }


  #[inline]
  pub fn fino(&self) -> u64 {
    // Safety:
    // Created from valid Table for this object
    // which contains a valid value in this slot
    unsafe { self._tab.get::<u64>(File::VT_FINO, Some(0)).unwrap()}
  }
  #[inline]
  pub fn name(&self) -> Option<&'a str> {
    // Safety:
    // Created from valid Table for this object
    // which contains a valid value in this slot
    unsafe { self._tab.get::<flatbuffers::ForwardsUOffset<&str>>(File::VT_NAME, None)}
  }
  #[inline]
  pub fn mode(&self) -> u32 {
    // Safety:
    // Created from valid Table for this object
    // which contains a valid value in this slot
    unsafe { self._tab.get::<u32>(File::VT_MODE, Some(0)).unwrap()}
  }
  #[inline]
  pub fn uid(&self) -> u32 {
    // Safety:
    // Created from valid Table for this object
    // which contains a valid value in this slot
    unsafe { self._tab.get::<u32>(File::VT_UID, Some(0)).unwrap()}
  }
  #[inline]
  pub fn gid(&self) -> u32 {
    // Safety:
    // Created from valid Table for this object
    // which contains a valid value in this slot
    unsafe { self._tab.get::<u32>(File::VT_GID, Some(0)).unwrap()}
  }
  #[inline]
  pub fn sz(&self) -> i64 {
    // Safety:
    // Created from valid Table for this object
    // which contains a valid value in this slot
    unsafe { self._tab.get::<i64>(File::VT_SZ, Some(0)).unwrap()}
  }
  #[inline]
  pub fn atim_sec(&self) -> i64 {
    // Safety:
    // Created from valid Table for this object
    // which contains a valid value in this slot
    unsafe { self._tab.get::<i64>(File::VT_ATIM_SEC, Some(0)).unwrap()}
  }
  #[inline]
  pub fn atim_nano(&self) -> i64 {
    // Safety:
    // Created from valid Table for this object
    // which contains a valid value in this slot
    unsafe { self._tab.get::<i64>(File::VT_ATIM_NANO, Some(0)).unwrap()}
  }
  #[inline]
  pub fn mtim_sec(&self) -> i64 {
    // Safety:
    // Created from valid Table for this object
    // which contains a valid value in this slot
    unsafe { self._tab.get::<i64>(File::VT_MTIM_SEC, Some(0)).unwrap()}
  }
  #[inline]
  pub fn mtim_nano(&self) -> i64 {
    // Safety:
    // Created from valid Table for this object
    // which contains a valid value in this slot
    unsafe { self._tab.get::<i64>(File::VT_MTIM_NANO, Some(0)).unwrap()}
  }
  #[inline]
  pub fn crc(&self) -> u32 {
    // Safety:
    // Created from valid Table for this object
    // which contains a valid value in this slot
    unsafe { self._tab.get::<u32>(File::VT_CRC, Some(0)).unwrap()}
  }
  #[inline]
  pub fn complete(&self) -> u32 {
    // Safety:
    // Created from valid Table for this object
    // which contains a valid value in this slot
    unsafe { self._tab.get::<u32>(File::VT_COMPLETE, Some(0)).unwrap()}
  }
}

impl flatbuffers::Verifiable for File<'_> {
  #[inline]
  fn run_verifier(
    v: &mut flatbuffers::Verifier, pos: usize
  ) -> Result<(), flatbuffers::InvalidFlatbuffer> {
    use self::flatbuffers::Verifiable;
    v.visit_table(pos)?
     .visit_field::<u64>("fino", Self::VT_FINO, false)?
     .visit_field::<flatbuffers::ForwardsUOffset<&str>>("name", Self::VT_NAME, false)?
     .visit_field::<u32>("mode", Self::VT_MODE, false)?
     .visit_field::<u32>("uid", Self::VT_UID, false)?
     .visit_field::<u32>("gid", Self::VT_GID, false)?
     .visit_field::<i64>("sz", Self::VT_SZ, false)?
     .visit_field::<i64>("atim_sec", Self::VT_ATIM_SEC, false)?
     .visit_field::<i64>("atim_nano", Self::VT_ATIM_NANO, false)?
     .visit_field::<i64>("mtim_sec", Self::VT_MTIM_SEC, false)?
     .visit_field::<i64>("mtim_nano", Self::VT_MTIM_NANO, false)?
     .visit_field::<u32>("crc", Self::VT_CRC, false)?
     .visit_field::<u32>("complete", Self::VT_COMPLETE, false)?
     .finish();
    Ok(())
  }
}
pub struct FileArgs<'a> {
    pub fino: u64,
    pub name: Option<flatbuffers::WIPOffset<&'a str>>,
    pub mode: u32,
    pub uid: u32,
    pub gid: u32,
    pub sz: i64,
    pub atim_sec: i64,
    pub atim_nano: i64,
    pub mtim_sec: i64,
    pub mtim_nano: i64,
    pub crc: u32,
    pub complete: u32,
}
impl<'a> Default for FileArgs<'a> {
  #[inline]
  fn default() -> Self {
    FileArgs {
      fino: 0,
      name: None,
      mode: 0,
      uid: 0,
      gid: 0,
      sz: 0,
      atim_sec: 0,
      atim_nano: 0,
      mtim_sec: 0,
      mtim_nano: 0,
      crc: 0,
      complete: 0,
    }
  }
}

pub struct FileBuilder<'a: 'b, 'b> {
  fbb_: &'b mut flatbuffers::FlatBufferBuilder<'a>,
  start_: flatbuffers::WIPOffset<flatbuffers::TableUnfinishedWIPOffset>,
}
impl<'a: 'b, 'b> FileBuilder<'a, 'b> {
  #[inline]
  pub fn add_fino(&mut self, fino: u64) {
    self.fbb_.push_slot::<u64>(File::VT_FINO, fino, 0);
  }
  #[inline]
  pub fn add_name(&mut self, name: flatbuffers::WIPOffset<&'b  str>) {
    self.fbb_.push_slot_always::<flatbuffers::WIPOffset<_>>(File::VT_NAME, name);
  }
  #[inline]
  pub fn add_mode(&mut self, mode: u32) {
    self.fbb_.push_slot::<u32>(File::VT_MODE, mode, 0);
  }
  #[inline]
  pub fn add_uid(&mut self, uid: u32) {
    self.fbb_.push_slot::<u32>(File::VT_UID, uid, 0);
  }
  #[inline]
  pub fn add_gid(&mut self, gid: u32) {
    self.fbb_.push_slot::<u32>(File::VT_GID, gid, 0);
  }
  #[inline]
  pub fn add_sz(&mut self, sz: i64) {
    self.fbb_.push_slot::<i64>(File::VT_SZ, sz, 0);
  }
  #[inline]
  pub fn add_atim_sec(&mut self, atim_sec: i64) {
    self.fbb_.push_slot::<i64>(File::VT_ATIM_SEC, atim_sec, 0);
  }
  #[inline]
  pub fn add_atim_nano(&mut self, atim_nano: i64) {
    self.fbb_.push_slot::<i64>(File::VT_ATIM_NANO, atim_nano, 0);
  }
  #[inline]
  pub fn add_mtim_sec(&mut self, mtim_sec: i64) {
    self.fbb_.push_slot::<i64>(File::VT_MTIM_SEC, mtim_sec, 0);
  }
  #[inline]
  pub fn add_mtim_nano(&mut self, mtim_nano: i64) {
    self.fbb_.push_slot::<i64>(File::VT_MTIM_NANO, mtim_nano, 0);
  }
  #[inline]
  pub fn add_crc(&mut self, crc: u32) {
    self.fbb_.push_slot::<u32>(File::VT_CRC, crc, 0);
  }
  #[inline]
  pub fn add_complete(&mut self, complete: u32) {
    self.fbb_.push_slot::<u32>(File::VT_COMPLETE, complete, 0);
  }
  #[inline]
  pub fn new(_fbb: &'b mut flatbuffers::FlatBufferBuilder<'a>) -> FileBuilder<'a, 'b> {
    let start = _fbb.start_table();
    FileBuilder {
      fbb_: _fbb,
      start_: start,
    }
  }
  #[inline]
  pub fn finish(self) -> flatbuffers::WIPOffset<File<'a>> {
    let o = self.fbb_.end_table(self.start_);
    flatbuffers::WIPOffset::new(o.value())
  }
}

impl core::fmt::Debug for File<'_> {
  fn fmt(&self, f: &mut core::fmt::Formatter<'_>) -> core::fmt::Result {
    let mut ds = f.debug_struct("File");
      ds.field("fino", &self.fino());
      ds.field("name", &self.name());
      ds.field("mode", &self.mode());
      ds.field("uid", &self.uid());
      ds.field("gid", &self.gid());
      ds.field("sz", &self.sz());
      ds.field("atim_sec", &self.atim_sec());
      ds.field("atim_nano", &self.atim_nano());
      ds.field("mtim_sec", &self.mtim_sec());
      ds.field("mtim_nano", &self.mtim_nano());
      ds.field("crc", &self.crc());
      ds.field("complete", &self.complete());
      ds.finish()
  }
}
#[inline]
/// Verifies that a buffer of bytes contains a `ESCP_file_list`
/// and returns it.
/// Note that verification is still experimental and may not
/// catch every error, or be maximally performant. For the
/// previous, unchecked, behavior use
/// `root_as_escp_file_list_unchecked`.
pub fn root_as_escp_file_list(buf: &[u8]) -> Result<ESCP_file_list, flatbuffers::InvalidFlatbuffer> {
  flatbuffers::root::<ESCP_file_list>(buf)
}
#[inline]
/// Verifies that a buffer of bytes contains a size prefixed
/// `ESCP_file_list` and returns it.
/// Note that verification is still experimental and may not
/// catch every error, or be maximally performant. For the
/// previous, unchecked, behavior use
/// `size_prefixed_root_as_escp_file_list_unchecked`.
pub fn size_prefixed_root_as_escp_file_list(buf: &[u8]) -> Result<ESCP_file_list, flatbuffers::InvalidFlatbuffer> {
  flatbuffers::size_prefixed_root::<ESCP_file_list>(buf)
}
#[inline]
/// Verifies, with the given options, that a buffer of bytes
/// contains a `ESCP_file_list` and returns it.
/// Note that verification is still experimental and may not
/// catch every error, or be maximally performant. For the
/// previous, unchecked, behavior use
/// `root_as_escp_file_list_unchecked`.
pub fn root_as_escp_file_list_with_opts<'b, 'o>(
  opts: &'o flatbuffers::VerifierOptions,
  buf: &'b [u8],
) -> Result<ESCP_file_list<'b>, flatbuffers::InvalidFlatbuffer> {
  flatbuffers::root_with_opts::<ESCP_file_list<'b>>(opts, buf)
}
#[inline]
/// Verifies, with the given verifier options, that a buffer of
/// bytes contains a size prefixed `ESCP_file_list` and returns
/// it. Note that verification is still experimental and may not
/// catch every error, or be maximally performant. For the
/// previous, unchecked, behavior use
/// `root_as_escp_file_list_unchecked`.
pub fn size_prefixed_root_as_escp_file_list_with_opts<'b, 'o>(
  opts: &'o flatbuffers::VerifierOptions,
  buf: &'b [u8],
) -> Result<ESCP_file_list<'b>, flatbuffers::InvalidFlatbuffer> {
  flatbuffers::size_prefixed_root_with_opts::<ESCP_file_list<'b>>(opts, buf)
}
#[inline]
/// Assumes, without verification, that a buffer of bytes contains a ESCP_file_list and returns it.
/// # Safety
/// Callers must trust the given bytes do indeed contain a valid `ESCP_file_list`.
pub unsafe fn root_as_escp_file_list_unchecked(buf: &[u8]) -> ESCP_file_list {
  flatbuffers::root_unchecked::<ESCP_file_list>(buf)
}
#[inline]
/// Assumes, without verification, that a buffer of bytes contains a size prefixed ESCP_file_list and returns it.
/// # Safety
/// Callers must trust the given bytes do indeed contain a valid size prefixed `ESCP_file_list`.
pub unsafe fn size_prefixed_root_as_escp_file_list_unchecked(buf: &[u8]) -> ESCP_file_list {
  flatbuffers::size_prefixed_root_unchecked::<ESCP_file_list>(buf)
}
#[inline]
pub fn finish_escp_file_list_buffer<'a, 'b>(
    fbb: &'b mut flatbuffers::FlatBufferBuilder<'a>,
    root: flatbuffers::WIPOffset<ESCP_file_list<'a>>) {
  fbb.finish(root, None);
}

#[inline]
pub fn finish_size_prefixed_escp_file_list_buffer<'a, 'b>(fbb: &'b mut flatbuffers::FlatBufferBuilder<'a>, root: flatbuffers::WIPOffset<ESCP_file_list<'a>>) {
  fbb.finish_size_prefixed(root, None);
}
