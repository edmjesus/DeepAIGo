#include "PolicyNet.h"

#include <fstream>

namespace DeepAIGo
{
	PolicyNet::PolicyNet()
		: process_({ ProcessorType::STONE_COLOR, ProcessorType::ONES, ProcessorType::TURNS_SINCE, ProcessorType::LIBERTIES,
			ProcessorType::CAPTURE_SIZE, ProcessorType::SELF_ATARI_SIZE, ProcessorType::LIBERTIES_AFTER_MOVE, ProcessorType::SENSIBLENESS,
			ProcessorType::ZEROS })
	{
		using namespace mxnet::cpp;

		Symbol data = Symbol::Variable("data");
		Symbol data_label = Symbol::Variable("softmax_label");

		std::vector<mxnet::cpp::Symbol> layers;
		layers.emplace_back(ConvFactory(data, 128, Shape(5, 5), Shape(1, 1), Shape(2, 2), "0"));

		for (int i = 1; i < 7; i++)
		{
			layers.emplace_back(
				ConvFactory(layers.back(), 128, Shape(3, 3), Shape(1, 1), Shape(1, 1), std::to_string(i)));
		}
	
		layers.emplace_back(
			ConvFactory(layers.back(), 1, Shape(1, 1), Shape(1, 1), Shape(0, 0), "7"));

		auto flatten = Flatten(layers.back());

		auto fc_w = Symbol("fullyconnected0_weight");
		auto fc_b = Symbol("fullyconnected0_bias");
		auto fc = FullyConnected(flatten, fc_w, fc_b, BOARD_SIZE2 + 1);

		net_ = SoftmaxOutput("softmax", fc, data_label);
	}

	PolicyNet::~PolicyNet()
	{
		if (exec_)
			delete exec_;
	}

	std::vector<ActionProb> PolicyNet::EvalState(const Board& board, int symmetric)
	{
		using namespace mxnet::cpp;

		auto input = make_input(board, symmetric);

		args_["data"].SyncCopyFromCPU(input.data(), input.num_elements());

		exec_->Forward(false);
		NDArray::WaitAll();
		
		std::vector<mx_float> output(BOARD_SIZE2 + 1);
		exec_->outputs[0].SyncCopyToCPU(output.data(), BOARD_SIZE2 + 1);

		std::vector<ActionProb> ret;

		for (size_t x = 0; x < BOARD_SIZE; ++x)
		{
			for (size_t y = 0; y < BOARD_SIZE; ++y)
			{
				auto pt = Point(x, y);

				if (board.IsValidMove(pt) && !board.IsTrueEye(pt, board.GetCurrentPlayer()))
				{
					ret.emplace_back(std::make_tuple(pt, output[symmetric_idx(Point(x, y), symmetric)]));
				}
			}
		}
		ret.emplace_back(std::make_tuple(Pass, output.back()));

		return ret;
	}

	void PolicyNet::InitNetwork()
	{
		using namespace mxnet::cpp;

		args_["data"] = NDArray(Shape(1, process_.GetOutputDim(), BOARD_SIZE, BOARD_SIZE), Context::cpu());
		args_["softmax_label"] = NDArray(Shape(1), Context::cpu());
		net_.InferArgsMap(Context::cpu(), &args_, args_);

		exec_ = net_.SimpleBind(Context::cpu(), args_);
		LoadCheckpoint("policy.params", exec_);
	}

	Tensor PolicyNet::make_input(const Board& board, int symmetric)
	{
		Tensor ret { boost::extents[process_.GetOutputDim()][BOARD_SIZE][BOARD_SIZE] };

		auto origin = process_.StateToTensor(board);

		switch (symmetric)
		{
		case 0: ret = origin; break;
		case 1: ret = Symmetrics::Rot90(origin); break;
		case 2: ret = Symmetrics::Rot180(origin); break;
		case 3: ret = Symmetrics::Rot270(origin); break;
		case 4: ret = Symmetrics::FlipUD(origin); break;
		case 5: ret = Symmetrics::FlipLR(origin); break;
		case 6: ret = Symmetrics::Diag1(origin); break;
		case 7: ret = Symmetrics::Diag2(origin); break;
		default: throw std::runtime_error("Invalid symmetric");
		}

		return ret;
	}

	size_t PolicyNet::symmetric_idx(const Point& pt, int symmetric) const
	{
		switch(symmetric)
		{
		case 0: return POS(pt);
		case 1: return POS(Point(pt.Y, BOARD_SIZE - pt.X - 1));
		case 2: return POS(Point(BOARD_SIZE - pt.X - 1, BOARD_SIZE - pt.Y - 1));
		case 3: return POS(Point(BOARD_SIZE - pt.Y - 1, pt.X));
		case 4: return POS(Point(pt.X, BOARD_SIZE - pt.Y - 1));
		case 5: return POS(Point(BOARD_SIZE - pt.X - 1, pt.Y));
		case 6: return POS(Point(pt.Y , pt.X));
		case 7: return POS(Point(pt.X, BOARD_SIZE - pt.Y - 1));
		default: return 0;
		}
	}
}
