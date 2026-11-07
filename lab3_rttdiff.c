// SPDX-License-Identifier: GPL-2.0
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/jiffies.h>
#include <linux/version.h>
#include <net/tcp.h>

#define DRV_NAME "lab3_rttdiff"

/* Tunables */
static int infl_thresh_permille = 150;  /* 15% RTT inflation threshold */
module_param(infl_thresh_permille, int, 0644);
static int beta_permille = 700;         /* β=0.7 multiplicative decrease */
module_param(beta_permille, int, 0644);
static int min_cwnd_pkts = 4;           /* minimum cwnd in packets */
module_param(min_cwnd_pkts, int, 0644);

struct rttdiff_ca {
	u32 min_rtt_us;
	u32 min_rtt_window_j;
};

static inline u32 get_srtt_us(const struct tcp_sock* tp)
{
#if LINUX_VERSION_CODE >= KERNEL_VERSION(4,9,0)
	return tp->srtt_us >> 3;
#else
	return tp->srtt_us >> 3;
#endif
}
static inline u32 nowj(void) { return jiffies; }

static void l3_init(struct sock* sk)
{
	struct tcp_sock* tp = tcp_sk(sk);
	struct rttdiff_ca* ca = inet_csk_ca(sk);
	ca->min_rtt_us = max(get_srtt_us(tp), 1U);
	ca->min_rtt_window_j = nowj();
}
static void l3_release(struct sock* sk) {}

static void l3_in_ack(struct sock* sk, u32 flags)
{
	struct tcp_sock* tp = tcp_sk(sk);
	struct rttdiff_ca* ca = inet_csk_ca(sk);
	u32 srtt = get_srtt_us(tp);

	/* refresh decayed minRTT every ~10s; track new minima */
	if (time_after(nowj(), ca->min_rtt_window_j + msecs_to_jiffies(10000))) {
		if (srtt) ca->min_rtt_us = srtt;
		ca->min_rtt_window_j = nowj();
	}
	else if (srtt && srtt < ca->min_rtt_us) {
		ca->min_rtt_us = srtt;
	}
}

static u32 l3_ssthresh(struct sock* sk)
{
	struct tcp_sock* tp = tcp_sk(sk);
	struct rttdiff_ca* ca = inet_csk_ca(sk);
	u32 srtt = max(get_srtt_us(tp), 1U);
	u32 minrtt = ca->min_rtt_us ? ca->min_rtt_us : srtt;
	u32 infl = (srtt > minrtt) ? (u32)(((u64)(srtt - minrtt) * 1000) / minrtt) : 0;

	if (infl < (u32)infl_thresh_permille) {
		u32 cw = max_t(u32, tp->snd_cwnd, (u32)min_cwnd_pkts);
		return max(cw - 1, (u32)min_cwnd_pkts);   /* gentle cut on random loss */
	}
	else {
		u32 ssth = (tp->snd_cwnd * (u32)beta_permille) / 1000;
		return max(ssth, (u32)min_cwnd_pkts);     /* multiplicative decrease on congestion */
	}
}

static void l3_cong_avoid(struct sock* sk, u32 ack, u32 acked)
{
	struct tcp_sock* tp = tcp_sk(sk);

	if (!tcp_is_cwnd_limited(sk)) return;
	if (tcp_in_slow_start(tp)) tcp_slow_start(tp, acked);
	else tcp_cong_avoid_ai(tp, tp->snd_cwnd, acked);

	if (tp->snd_cwnd < (u32)min_cwnd_pkts) tp->snd_cwnd = (u32)min_cwnd_pkts;
}
static u32 l3_undo_cwnd(struct sock* sk) { return tcp_sk(sk)->snd_cwnd; }
static void l3_set_state(struct sock* sk, u8 new_state) {}

static struct tcp_congestion_ops l3_ops __read_mostly = {
	.init = l3_init,
	.release = l3_release,
	.in_ack_event = l3_in_ack,
	.ssthresh = l3_ssthresh,
	.cong_avoid = l3_cong_avoid,
	.undo_cwnd = l3_undo_cwnd,
	.set_state = l3_set_state,
	.owner = THIS_MODULE,
	.name = DRV_NAME,
};

static int __init l3_register(void) { pr_info(DRV_NAME ": registering\n"); return tcp_register_congestion_control(&l3_ops); }
static void __exit l3_unregister(void) { tcp_unregister_congestion_control(&l3_ops); pr_info(DRV_NAME ": unregistered\n"); }
module_init(l3_register); module_exit(l3_unregister);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("RTT-trend loss differentiation TCP CC (lab3_rttdiff)");


