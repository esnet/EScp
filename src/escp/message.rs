// automatically generated by the FlatBuffers compiler, do not modify


// @generated

use core::mem;
use core::cmp::Ordering;

extern crate flatbuffers;
use self::flatbuffers::{EndianScalar, Follow};

pub enum Message_listOffset {}
#[derive(Copy, Clone, PartialEq)]

pub struct Message_list<'a> {
  pub _tab: flatbuffers::Table<'a>,
}

impl<'a> flatbuffers::Follow<'a> for Message_list<'a> {
  type Inner = Message_list<'a>;
  #[inline]
  unsafe fn follow(buf: &'a [u8], loc: usize) -> Self::Inner {
    Self { _tab: flatbuffers::Table::new(buf, loc) }
  }
}

impl<'a> Message_list<'a> {
  pub const VT_MESSAGES: flatbuffers::VOffsetT = 4;

  #[inline]
  pub unsafe fn init_from_table(table: flatbuffers::Table<'a>) -> Self {
    Message_list { _tab: table }
  }
  #[allow(unused_mut)]
  pub fn create<'bldr: 'args, 'args: 'mut_bldr, 'mut_bldr>(
    _fbb: &'mut_bldr mut flatbuffers::FlatBufferBuilder<'bldr>,
    args: &'args Message_listArgs<'args>
  ) -> flatbuffers::WIPOffset<Message_list<'bldr>> {
    let mut builder = Message_listBuilder::new(_fbb);
    if let Some(x) = args.messages { builder.add_messages(x); }
    builder.finish()
  }


  #[inline]
  pub fn messages(&self) -> Option<flatbuffers::Vector<'a, flatbuffers::ForwardsUOffset<Message<'a>>>> {
    // Safety:
    // Created from valid Table for this object
    // which contains a valid value in this slot
    unsafe { self._tab.get::<flatbuffers::ForwardsUOffset<flatbuffers::Vector<'a, flatbuffers::ForwardsUOffset<Message>>>>(Message_list::VT_MESSAGES, None)}
  }
}

impl flatbuffers::Verifiable for Message_list<'_> {
  #[inline]
  fn run_verifier(
    v: &mut flatbuffers::Verifier, pos: usize
  ) -> Result<(), flatbuffers::InvalidFlatbuffer> {
    use self::flatbuffers::Verifiable;
    v.visit_table(pos)?
     .visit_field::<flatbuffers::ForwardsUOffset<flatbuffers::Vector<'_, flatbuffers::ForwardsUOffset<Message>>>>("messages", Self::VT_MESSAGES, false)?
     .finish();
    Ok(())
  }
}
pub struct Message_listArgs<'a> {
    pub messages: Option<flatbuffers::WIPOffset<flatbuffers::Vector<'a, flatbuffers::ForwardsUOffset<Message<'a>>>>>,
}
impl<'a> Default for Message_listArgs<'a> {
  #[inline]
  fn default() -> Self {
    Message_listArgs {
      messages: None,
    }
  }
}

pub struct Message_listBuilder<'a: 'b, 'b> {
  fbb_: &'b mut flatbuffers::FlatBufferBuilder<'a>,
  start_: flatbuffers::WIPOffset<flatbuffers::TableUnfinishedWIPOffset>,
}
impl<'a: 'b, 'b> Message_listBuilder<'a, 'b> {
  #[inline]
  pub fn add_messages(&mut self, messages: flatbuffers::WIPOffset<flatbuffers::Vector<'b , flatbuffers::ForwardsUOffset<Message<'b >>>>) {
    self.fbb_.push_slot_always::<flatbuffers::WIPOffset<_>>(Message_list::VT_MESSAGES, messages);
  }
  #[inline]
  pub fn new(_fbb: &'b mut flatbuffers::FlatBufferBuilder<'a>) -> Message_listBuilder<'a, 'b> {
    let start = _fbb.start_table();
    Message_listBuilder {
      fbb_: _fbb,
      start_: start,
    }
  }
  #[inline]
  pub fn finish(self) -> flatbuffers::WIPOffset<Message_list<'a>> {
    let o = self.fbb_.end_table(self.start_);
    flatbuffers::WIPOffset::new(o.value())
  }
}

impl core::fmt::Debug for Message_list<'_> {
  fn fmt(&self, f: &mut core::fmt::Formatter<'_>) -> core::fmt::Result {
    let mut ds = f.debug_struct("Message_list");
      ds.field("messages", &self.messages());
      ds.finish()
  }
}
pub enum MessageOffset {}
#[derive(Copy, Clone, PartialEq)]

pub struct Message<'a> {
  pub _tab: flatbuffers::Table<'a>,
}

impl<'a> flatbuffers::Follow<'a> for Message<'a> {
  type Inner = Message<'a>;
  #[inline]
  unsafe fn follow(buf: &'a [u8], loc: usize) -> Self::Inner {
    Self { _tab: flatbuffers::Table::new(buf, loc) }
  }
}

impl<'a> Message<'a> {
  pub const VT_IS_ERROR: flatbuffers::VOffsetT = 4;
  pub const VT_IS_WARNING: flatbuffers::VOffsetT = 6;
  pub const VT_TYPE_: flatbuffers::VOffsetT = 8;
  pub const VT_CODE: flatbuffers::VOffsetT = 10;
  pub const VT_MESSAGE: flatbuffers::VOffsetT = 12;

  #[inline]
  pub unsafe fn init_from_table(table: flatbuffers::Table<'a>) -> Self {
    Message { _tab: table }
  }
  #[allow(unused_mut)]
  pub fn create<'bldr: 'args, 'args: 'mut_bldr, 'mut_bldr>(
    _fbb: &'mut_bldr mut flatbuffers::FlatBufferBuilder<'bldr>,
    args: &'args MessageArgs<'args>
  ) -> flatbuffers::WIPOffset<Message<'bldr>> {
    let mut builder = MessageBuilder::new(_fbb);
    if let Some(x) = args.message { builder.add_message(x); }
    if let Some(x) = args.code { builder.add_code(x); }
    builder.add_type_(args.type_);
    builder.add_is_warning(args.is_warning);
    builder.add_is_error(args.is_error);
    builder.finish()
  }


  #[inline]
  pub fn is_error(&self) -> bool {
    // Safety:
    // Created from valid Table for this object
    // which contains a valid value in this slot
    unsafe { self._tab.get::<bool>(Message::VT_IS_ERROR, Some(false)).unwrap()}
  }
  #[inline]
  pub fn is_warning(&self) -> bool {
    // Safety:
    // Created from valid Table for this object
    // which contains a valid value in this slot
    unsafe { self._tab.get::<bool>(Message::VT_IS_WARNING, Some(false)).unwrap()}
  }
  #[inline]
  pub fn type_(&self) -> i32 {
    // Safety:
    // Created from valid Table for this object
    // which contains a valid value in this slot
    unsafe { self._tab.get::<i32>(Message::VT_TYPE_, Some(0)).unwrap()}
  }
  #[inline]
  pub fn code(&self) -> Option<flatbuffers::Vector<'a, i64>> {
    // Safety:
    // Created from valid Table for this object
    // which contains a valid value in this slot
    unsafe { self._tab.get::<flatbuffers::ForwardsUOffset<flatbuffers::Vector<'a, i64>>>(Message::VT_CODE, None)}
  }
  #[inline]
  pub fn message(&self) -> Option<&'a str> {
    // Safety:
    // Created from valid Table for this object
    // which contains a valid value in this slot
    unsafe { self._tab.get::<flatbuffers::ForwardsUOffset<&str>>(Message::VT_MESSAGE, None)}
  }
}

impl flatbuffers::Verifiable for Message<'_> {
  #[inline]
  fn run_verifier(
    v: &mut flatbuffers::Verifier, pos: usize
  ) -> Result<(), flatbuffers::InvalidFlatbuffer> {
    use self::flatbuffers::Verifiable;
    v.visit_table(pos)?
     .visit_field::<bool>("is_error", Self::VT_IS_ERROR, false)?
     .visit_field::<bool>("is_warning", Self::VT_IS_WARNING, false)?
     .visit_field::<i32>("type_", Self::VT_TYPE_, false)?
     .visit_field::<flatbuffers::ForwardsUOffset<flatbuffers::Vector<'_, i64>>>("code", Self::VT_CODE, false)?
     .visit_field::<flatbuffers::ForwardsUOffset<&str>>("message", Self::VT_MESSAGE, false)?
     .finish();
    Ok(())
  }
}
pub struct MessageArgs<'a> {
    pub is_error: bool,
    pub is_warning: bool,
    pub type_: i32,
    pub code: Option<flatbuffers::WIPOffset<flatbuffers::Vector<'a, i64>>>,
    pub message: Option<flatbuffers::WIPOffset<&'a str>>,
}
impl<'a> Default for MessageArgs<'a> {
  #[inline]
  fn default() -> Self {
    MessageArgs {
      is_error: false,
      is_warning: false,
      type_: 0,
      code: None,
      message: None,
    }
  }
}

pub struct MessageBuilder<'a: 'b, 'b> {
  fbb_: &'b mut flatbuffers::FlatBufferBuilder<'a>,
  start_: flatbuffers::WIPOffset<flatbuffers::TableUnfinishedWIPOffset>,
}
impl<'a: 'b, 'b> MessageBuilder<'a, 'b> {
  #[inline]
  pub fn add_is_error(&mut self, is_error: bool) {
    self.fbb_.push_slot::<bool>(Message::VT_IS_ERROR, is_error, false);
  }
  #[inline]
  pub fn add_is_warning(&mut self, is_warning: bool) {
    self.fbb_.push_slot::<bool>(Message::VT_IS_WARNING, is_warning, false);
  }
  #[inline]
  pub fn add_type_(&mut self, type_: i32) {
    self.fbb_.push_slot::<i32>(Message::VT_TYPE_, type_, 0);
  }
  #[inline]
  pub fn add_code(&mut self, code: flatbuffers::WIPOffset<flatbuffers::Vector<'b , i64>>) {
    self.fbb_.push_slot_always::<flatbuffers::WIPOffset<_>>(Message::VT_CODE, code);
  }
  #[inline]
  pub fn add_message(&mut self, message: flatbuffers::WIPOffset<&'b  str>) {
    self.fbb_.push_slot_always::<flatbuffers::WIPOffset<_>>(Message::VT_MESSAGE, message);
  }
  #[inline]
  pub fn new(_fbb: &'b mut flatbuffers::FlatBufferBuilder<'a>) -> MessageBuilder<'a, 'b> {
    let start = _fbb.start_table();
    MessageBuilder {
      fbb_: _fbb,
      start_: start,
    }
  }
  #[inline]
  pub fn finish(self) -> flatbuffers::WIPOffset<Message<'a>> {
    let o = self.fbb_.end_table(self.start_);
    flatbuffers::WIPOffset::new(o.value())
  }
}

impl core::fmt::Debug for Message<'_> {
  fn fmt(&self, f: &mut core::fmt::Formatter<'_>) -> core::fmt::Result {
    let mut ds = f.debug_struct("Message");
      ds.field("is_error", &self.is_error());
      ds.field("is_warning", &self.is_warning());
      ds.field("type_", &self.type_());
      ds.field("code", &self.code());
      ds.field("message", &self.message());
      ds.finish()
  }
}
#[inline]
/// Verifies that a buffer of bytes contains a `Message_list`
/// and returns it.
/// Note that verification is still experimental and may not
/// catch every error, or be maximally performant. For the
/// previous, unchecked, behavior use
/// `root_as_message_list_unchecked`.
pub fn root_as_message_list(buf: &[u8]) -> Result<Message_list, flatbuffers::InvalidFlatbuffer> {
  flatbuffers::root::<Message_list>(buf)
}
#[inline]
/// Verifies that a buffer of bytes contains a size prefixed
/// `Message_list` and returns it.
/// Note that verification is still experimental and may not
/// catch every error, or be maximally performant. For the
/// previous, unchecked, behavior use
/// `size_prefixed_root_as_message_list_unchecked`.
pub fn size_prefixed_root_as_message_list(buf: &[u8]) -> Result<Message_list, flatbuffers::InvalidFlatbuffer> {
  flatbuffers::size_prefixed_root::<Message_list>(buf)
}
#[inline]
/// Verifies, with the given options, that a buffer of bytes
/// contains a `Message_list` and returns it.
/// Note that verification is still experimental and may not
/// catch every error, or be maximally performant. For the
/// previous, unchecked, behavior use
/// `root_as_message_list_unchecked`.
pub fn root_as_message_list_with_opts<'b, 'o>(
  opts: &'o flatbuffers::VerifierOptions,
  buf: &'b [u8],
) -> Result<Message_list<'b>, flatbuffers::InvalidFlatbuffer> {
  flatbuffers::root_with_opts::<Message_list<'b>>(opts, buf)
}
#[inline]
/// Verifies, with the given verifier options, that a buffer of
/// bytes contains a size prefixed `Message_list` and returns
/// it. Note that verification is still experimental and may not
/// catch every error, or be maximally performant. For the
/// previous, unchecked, behavior use
/// `root_as_message_list_unchecked`.
pub fn size_prefixed_root_as_message_list_with_opts<'b, 'o>(
  opts: &'o flatbuffers::VerifierOptions,
  buf: &'b [u8],
) -> Result<Message_list<'b>, flatbuffers::InvalidFlatbuffer> {
  flatbuffers::size_prefixed_root_with_opts::<Message_list<'b>>(opts, buf)
}
#[inline]
/// Assumes, without verification, that a buffer of bytes contains a Message_list and returns it.
/// # Safety
/// Callers must trust the given bytes do indeed contain a valid `Message_list`.
pub unsafe fn root_as_message_list_unchecked(buf: &[u8]) -> Message_list {
  flatbuffers::root_unchecked::<Message_list>(buf)
}
#[inline]
/// Assumes, without verification, that a buffer of bytes contains a size prefixed Message_list and returns it.
/// # Safety
/// Callers must trust the given bytes do indeed contain a valid size prefixed `Message_list`.
pub unsafe fn size_prefixed_root_as_message_list_unchecked(buf: &[u8]) -> Message_list {
  flatbuffers::size_prefixed_root_unchecked::<Message_list>(buf)
}
#[inline]
pub fn finish_message_list_buffer<'a, 'b>(
    fbb: &'b mut flatbuffers::FlatBufferBuilder<'a>,
    root: flatbuffers::WIPOffset<Message_list<'a>>) {
  fbb.finish(root, None);
}

#[inline]
pub fn finish_size_prefixed_message_list_buffer<'a, 'b>(fbb: &'b mut flatbuffers::FlatBufferBuilder<'a>, root: flatbuffers::WIPOffset<Message_list<'a>>) {
  fbb.finish_size_prefixed(root, None);
}
