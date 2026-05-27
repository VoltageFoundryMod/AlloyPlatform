#include "ii_follower.h"

#include <cstring>

namespace fourseas
{

// Singleton pointer for ISR -> class dispatch
static IIFollower *g_ii_instance = nullptr;

void IIFollower::Init(uint8_t address)
{
    g_ii_instance = this;
    state_.Reset();
    std::memset(rx_buf_, 0, sizeof(rx_buf_));
    std::memset(tx_buf_, 0, sizeof(tx_buf_));
    rx_size_   = 0;
    rx_idx_    = 0;
    tx_idx_    = 0;
    tx_size_   = 0;
    listening_ = false;

    // Configure I2C4 GPIO and clocks manually. We bypass both
    // libDaisy's I2CHandle::Init() and HAL_I2C_Init() entirely to
    // avoid populating libDaisy's static i2c_handles[3] array.
    // If that array were populated, libDaisy's global callbacks
    // (SlaveRxCpltCallback, SlaveTxCpltCallback, ErrorCallback)
    // would call DmaTransferFinished() for I2C4 events, corrupting
    // the shared DMA state machine and deadlocking I2C1.
    //
    // We also bypass HAL's IRQ handlers (HAL_I2C_EV_IRQHandler,
    // HAL_I2C_ER_IRQHandler) for the same reason -- they dispatch
    // to I2C_Slave_ISR_IT which calls those same global callbacks.
    // Instead, our IRQ handlers operate directly on I2C4 registers.
    __HAL_RCC_GPIOB_CLK_ENABLE();
    __HAL_RCC_I2C4_CLK_ENABLE();

    GPIO_InitTypeDef gpio = {};
    gpio.Mode             = GPIO_MODE_AF_OD;
    gpio.Pull             = GPIO_NOPULL;
    gpio.Speed            = GPIO_SPEED_FREQ_LOW;
    gpio.Alternate        = GPIO_AF6_I2C4;

    gpio.Pin = GPIO_PIN_6; // SCL - Daisy Seed D13
    HAL_GPIO_Init(GPIOB, &gpio);
    gpio.Pin = GPIO_PIN_7; // SDA - Daisy Seed D14
    HAL_GPIO_Init(GPIOB, &gpio);

    // Disable I2C4 peripheral for configuration
    I2C4->CR1 = 0;

    // Timing for 400kHz (from libDaisy, depends on PCLK1 frequency)
    uint32_t timing = (SystemCoreClock > 400000000) ? 0x30B00F2D : 0x20D01132;
    I2C4->TIMINGR   = timing;

    // Own address 1 (7-bit mode, enabled)
    I2C4->OAR1 = I2C_OAR1_OA1EN | ((uint32_t)address << 1);

    // No dual address, no general call, clock stretch enabled
    I2C4->OAR2 = 0;
    I2C4->CR2  = 0;

    // Enable I2C4 peripheral (analog filter on, no digital filter)
    I2C4->CR1 = I2C_CR1_PE;

    // Enable I2C4 event and error interrupts
    // Priority 2,0: below audio DMA (0,0) and internal peripherals (1,1)
    HAL_NVIC_SetPriority(I2C4_EV_IRQn, 2, 0);
    HAL_NVIC_EnableIRQ(I2C4_EV_IRQn);
    HAL_NVIC_SetPriority(I2C4_ER_IRQn, 2, 0);
    HAL_NVIC_EnableIRQ(I2C4_ER_IRQn);
}

void IIFollower::StartListening()
{ EnableListen(); }

void IIFollower::EnableListen()
{
    rx_idx_    = 0;
    rx_size_   = 0;
    tx_idx_    = 0;
    tx_size_   = 0;
    listening_ = true;

    // Enable address match, stop, NACK, and error interrupts
    I2C4->CR1
        |= I2C_CR1_ADDRIE | I2C_CR1_STOPIE | I2C_CR1_NACKIE | I2C_CR1_ERRIE;
}

void IIFollower::HandleEventIRQ()
{
    uint32_t isr = I2C4->ISR;

    // Single-event-per-ISR with explicit priority. The IRQ refires
    // immediately for any other pending flag, so multiple coincident
    // events are processed in this order across separate ISR entries:
    //
    //   1. RXNE  - drain any pending byte before STOPF reads rx_idx_
    //   2. NACKF - master ended read; clean up TX state
    //   3. STOPF - finalize previous transaction (ProcessCommand for writes)
    //   4. ADDR  - set up for new transaction
    //   5. TXIS  - supply TX data byte
    //
    // Required because STM32 I2C events can pile up when audio DMA
    // (priority 0,0) preempts this ISR (priority 2,0). Processing all
    // pending flags in a single ISR pass would let STOPF's cleanup of
    // the previous transaction clobber RX/TX state that an ADDR for a
    // new transaction has already armed, silently corrupting reads
    // (master sees 0) and writes (bytes never captured).

    // 1. RX not empty - byte received from master. Must drain before
    //    STOPF, else the last byte of a write transaction is lost.
    if(isr & I2C_ISR_RXNE)
    {
        if(rx_idx_ < kIIMaxMessageSize)
        {
            rx_buf_[rx_idx_++] = static_cast<uint8_t>(I2C4->RXDR);
        }
        else
        {
            // Overflow: read and discard
            (void)I2C4->RXDR;
        }
        return;
    }

    // 2. NACK received (master ended read early)
    if(isr & I2C_ISR_NACKF)
    {
        // Disable TX interrupt
        I2C4->CR1 &= ~I2C_CR1_TXIE;
        // Flush TX register
        I2C4->ISR |= I2C_ISR_TXE;
        // Clear NACK flag
        I2C4->ICR = I2C_ICR_NACKCF;
        return;
    }

    // 3. STOP condition - finalize the previous transaction
    if(isr & I2C_ISR_STOPF)
    {
        // Disable RX/TX interrupts; next transaction's ADDR re-arms
        // the appropriate one for its direction.
        I2C4->CR1 &= ~(I2C_CR1_RXIE | I2C_CR1_TXIE);
        // Flush TX register
        I2C4->ISR |= I2C_ISR_TXE;
        // Clear STOP flag
        I2C4->ICR = I2C_ICR_STOPCF;

        // Process received write data
        rx_size_ = rx_idx_;
        if(rx_size_ > 0 && !(rx_buf_[0] & kIIQueryFlag))
        {
            ProcessCommand();
        }

        // Re-enable listening
        EnableListen();
        return;
    }

    // 4. Address match - start of a new transaction. Hardware clock-
    //    stretches while ADDR is set, so RXNE/TXIS for the new
    //    transaction cannot coincide with this in the same ISR pass.
    if(isr & I2C_ISR_ADDR)
    {
        uint8_t dir = (isr & I2C_ISR_DIR) ? 1 : 0; // 1=read(TX), 0=write(RX)

        if(dir == 0)
        {
            // Master is writing to us (RX)
            std::memset(rx_buf_, 0, sizeof(rx_buf_));
            rx_idx_  = 0;
            rx_size_ = 0;
            // Enable RXNE interrupt
            I2C4->CR1 |= I2C_CR1_RXIE;
        }
        else
        {
            // Master is reading from us (TX) - query response
            uint8_t cmd = rx_buf_[0] & ~kIIQueryFlag;
            PrepareQueryResponse(cmd);
            tx_idx_  = 0;
            tx_size_ = 2;
            // Enable TXIS interrupt, flush TX register
            I2C4->ISR |= I2C_ISR_TXE; // flush TXDR
            I2C4->CR1 |= I2C_CR1_TXIE;
        }

        // Clear ADDR flag
        I2C4->ICR = I2C_ICR_ADDRCF;
        return;
    }

    // 5. TX interrupt status - master wants to read a byte
    if(isr & I2C_ISR_TXIS)
    {
        if(tx_idx_ < tx_size_)
        {
            I2C4->TXDR = tx_buf_[tx_idx_++];
        }
        else
        {
            // No more data, send 0
            I2C4->TXDR = 0x00;
        }
        return;
    }
}

void IIFollower::HandleErrorIRQ()
{
    // Clear all error flags
    uint32_t isr = I2C4->ISR;

    if(isr & I2C_ISR_BERR)
    {
        I2C4->ICR = I2C_ICR_BERRCF;
    }
    if(isr & I2C_ISR_OVR)
    {
        I2C4->ICR = I2C_ICR_OVRCF;
    }
    if(isr & I2C_ISR_ARLO)
    {
        I2C4->ICR = I2C_ICR_ARLOCF;
    }

    // Disable RX/TX interrupts
    I2C4->CR1 &= ~(I2C_CR1_RXIE | I2C_CR1_TXIE);
    // Flush TX register
    I2C4->ISR |= I2C_ISR_TXE;

    // Re-enable listening
    EnableListen();
}

void IIFollower::Poll()
{
    // Recover from unexpected states where listen mode has dropped out.
    // Check if address match interrupt is still enabled; if not, re-enable.
    if(listening_ && !(I2C4->CR1 & I2C_CR1_ADDRIE))
    {
        EnableListen();
    }
}

void IIFollower::ProcessCommand()
{
    uint8_t cmd = rx_buf_[0];

    // Mark I2C control as active (unless this is a RELEASE)
    if(cmd != II_RELEASE)
    {
        state_.active = true;
    }

    switch(cmd)
    {
        // Global 16-bit parameters (cmd + 2 bytes MSB-first)
        case II_X:
            if(rx_size_ >= 3)
                state_.x = ReadInt16(1);
            break;
        case II_Y:
            if(rx_size_ >= 3)
                state_.y = ReadInt16(1);
            break;
        case II_Z:
            if(rx_size_ >= 3)
                state_.z = ReadInt16(1);
            break;
        case II_X_SPREAD:
            if(rx_size_ >= 3)
                state_.x_spread = ReadInt16(1);
            break;
        case II_Y_SPREAD:
            if(rx_size_ >= 3)
                state_.y_spread = ReadInt16(1);
            break;
        case II_Z_SPREAD:
            if(rx_size_ >= 3)
                state_.z_spread = ReadInt16(1);
            break;
        case II_TUNE:
            if(rx_size_ >= 3)
                state_.tune = ReadInt16(1);
            break;
        case II_MOD_DEPTH_1:
            if(rx_size_ >= 3)
                state_.mod_depth_1 = ReadInt16(1);
            break;
        case II_MOD_DEPTH_2:
            if(rx_size_ >= 3)
                state_.mod_depth_2 = ReadInt16(1);
            break;
        case II_FREQ_SPREAD:
            if(rx_size_ >= 3)
                state_.freq_spread = ReadInt16(1);
            break;

        // 8-bit parameters: Teletype sends all values as 16-bit MSB-first,
        // so [cmd, MSB, LSB]. The actual value is the low byte (rx_buf_[2]).
        case II_BANK:
            if(rx_size_ >= 3)
                state_.bank = static_cast<int8_t>(rx_buf_[2]);
            break;
        case II_SYNC_1:
            if(rx_size_ >= 3)
                state_.sync_1 = static_cast<int8_t>(rx_buf_[2]);
            break;
        case II_SYNC_2:
            if(rx_size_ >= 3)
                state_.sync_2 = static_cast<int8_t>(rx_buf_[2]);
            break;
        case II_MOD_1:
            if(rx_size_ >= 3)
                state_.mod_1 = static_cast<int8_t>(rx_buf_[2]);
            break;
        case II_MOD_2:
            if(rx_size_ >= 3)
                state_.mod_2 = static_cast<int8_t>(rx_buf_[2]);
            break;
        case II_LFO_1:
            if(rx_size_ >= 3)
                state_.lfo_1 = static_cast<int8_t>(rx_buf_[2]);
            break;
        case II_LFO_2:
            if(rx_size_ >= 3)
                state_.lfo_2 = static_cast<int8_t>(rx_buf_[2]);
            break;
        case II_INTERP:
            if(rx_size_ >= 3)
                state_.interp = static_cast<int8_t>(rx_buf_[2]);
            break;
        case II_SPREAD_TYPE:
            if(rx_size_ >= 3)
                state_.spread_type = static_cast<int8_t>(rx_buf_[2]);
            break;

        // Per-oscillator parameters: Teletype IIS2 sends
        // [cmd, osc_MSB, osc_LSB, val_MSB, val_LSB] = 5 bytes.
        // Osc index is the low byte of the first 16-bit arg (rx_buf_[2]).
        // Value is the second 16-bit arg at offset 3.
        case II_OSC_NOTE:
            if(rx_size_ >= 5 && rx_buf_[2] < 4)
                state_.osc[rx_buf_[2]].note = ReadInt16(3);
            break;
        case II_OSC_X:
            if(rx_size_ >= 5 && rx_buf_[2] < 4)
                state_.osc[rx_buf_[2]].x = ReadInt16(3);
            break;
        case II_OSC_Y:
            if(rx_size_ >= 5 && rx_buf_[2] < 4)
                state_.osc[rx_buf_[2]].y = ReadInt16(3);
            break;
        case II_OSC_Z:
            if(rx_size_ >= 5 && rx_buf_[2] < 4)
                state_.osc[rx_buf_[2]].z = ReadInt16(3);
            break;

        // Control
        case II_RELEASE: state_.Reset(); break;

        default: break;
    }
}

void IIFollower::PrepareQueryResponse(uint8_t cmd)
{
    int16_t val = 0;

    switch(cmd)
    {
        case II_X: val = state_.x; break;
        case II_Y: val = state_.y; break;
        case II_Z: val = state_.z; break;
        case II_X_SPREAD: val = state_.x_spread; break;
        case II_Y_SPREAD: val = state_.y_spread; break;
        case II_Z_SPREAD: val = state_.z_spread; break;
        case II_TUNE: val = state_.tune; break;
        case II_MOD_DEPTH_1: val = state_.mod_depth_1; break;
        case II_MOD_DEPTH_2: val = state_.mod_depth_2; break;
        case II_FREQ_SPREAD: val = state_.freq_spread; break;
        // Mode/bank queries return the live mirror (current effective
        // state), not the command inbox — main loop consumes the inbox
        // on apply and reflects current state into *_live every tick.
        case II_BANK: val = state_.bank_live; break;
        case II_SYNC_1: val = state_.sync_1_live; break;
        case II_SYNC_2: val = state_.sync_2_live; break;
        case II_MOD_1: val = state_.mod_1_live; break;
        case II_MOD_2: val = state_.mod_2_live; break;
        case II_LFO_1: val = state_.lfo_1_live; break;
        case II_LFO_2: val = state_.lfo_2_live; break;
        case II_INTERP: val = state_.interp_live; break;
        case II_SPREAD_TYPE: val = state_.spread_type_live; break;

        // Per-oscillator queries. Wire format for the query write
        // phase is [cmd|0x80, osc_MSB, osc_LSB] (3 bytes); rx_buf_ is
        // preserved across STOPF, so rx_buf_[2] still holds the osc
        // index when this runs from the read phase's ADDR handler.
        case II_OSC_NOTE:
            if(rx_buf_[2] < 4)
            {
                val = state_.osc[rx_buf_[2]].note;
            }
            break;
        case II_OSC_X:
            if(rx_buf_[2] < 4)
            {
                val = state_.osc[rx_buf_[2]].x;
            }
            break;
        case II_OSC_Y:
            if(rx_buf_[2] < 4)
            {
                val = state_.osc[rx_buf_[2]].y;
            }
            break;
        case II_OSC_Z:
            if(rx_buf_[2] < 4)
            {
                val = state_.osc[rx_buf_[2]].z;
            }
            break;

        default: val = 0; break;
    }

    // Coerce the 16-bit "unset" sentinel to 0 for queries. Unset and
    // "0 offset" have identical audio effect, so returning INT16_MIN is
    // just surprising. 8-bit fields (sync/mod/lfo/interp/spread_type/bank)
    // keep their kI2CUnset8 (-1) on query: for mode fields, -1 meaningfully
    // distinguishes "hardware passthrough" from valid mode values (0..N).
    if(val == kI2CUnset16)
    {
        val = 0;
    }

    // MSB-first
    tx_buf_[0] = static_cast<uint8_t>((static_cast<uint16_t>(val) >> 8) & 0xFF);
    tx_buf_[1] = static_cast<uint8_t>(static_cast<uint16_t>(val) & 0xFF);
}

} // namespace fourseas

// ============================================================================
// extern "C" IRQ handlers
//
// We handle I2C4 interrupts entirely at the register level, bypassing
// HAL_I2C_EV_IRQHandler and HAL_I2C_ER_IRQHandler. This is necessary
// because HAL's ISR dispatches to I2C_Slave_ISR_IT, which calls global
// callbacks (HAL_I2C_SlaveRxCpltCallback, SlaveTxCpltCallback,
// ErrorCallback) that libDaisy overrides. Those overrides call
// DmaTransferFinished(), which corrupts the shared DMA state machine
// used by I2C1 (LED driver, GPIO expander), deadlocking the system.
//
// HAL_I2C_AddrCallback and HAL_I2C_ListenCpltCallback are weak/unused
// by libDaisy, but we no longer need them since we handle ADDR and
// STOPF directly in the register-level ISR.
// ============================================================================
extern "C"
{
    void I2C4_EV_IRQHandler(void)
    {
        if(fourseas::g_ii_instance != nullptr)
        {
            fourseas::g_ii_instance->HandleEventIRQ();
        }
    }

    void I2C4_ER_IRQHandler(void)
    {
        if(fourseas::g_ii_instance != nullptr)
        {
            fourseas::g_ii_instance->HandleErrorIRQ();
        }
    }
}
