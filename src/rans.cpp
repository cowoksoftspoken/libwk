
#include "rans.h"

namespace wk {


template class RansTable<10>;
template class RansTable<11>;
template class RansTable<12>;
template class RansTable<14>;

template class RansEncoder<10>;
template class RansEncoder<11>;
template class RansEncoder<12>;
template class RansEncoder<14>;

template class RansDecoder<10>;
template class RansDecoder<11>;
template class RansDecoder<12>;
template class RansDecoder<14>;

template class ContextRansTables<10>;
template class ContextRansTables<11>;
template class ContextRansTables<12>;
template class ContextRansTables<14>;

}
