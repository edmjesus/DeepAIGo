#include "PolicyTrainer.h"

#include <chrono>
#include <iostream>
#include <stdio.h>

using namespace DeepAIGo;
namespace mxcpp = mxnet::cpp;

PolicyTrainer::PolicyTrainer(const std::string& train, const std::string& test, int batch_size, int epoch)
	: epoch_(epoch), batch_size_(batch_size), PolicyNet()
{
	train_ = std::make_shared<HDF5Provider>(train, batch_size);
	test_ = std::make_shared<HDF5Provider>(test, batch_size);

	InitNetwork();
}

PolicyTrainer::~PolicyTrainer()
{
}

void PolicyTrainer::Train(float lr, const std::string& output)
{
	mxcpp::Optimizer* opt = mxcpp::OptimizerRegistry::Find("ccsgd");
	opt->SetParam("lr", lr)
		->SetParam("wd", 1e-4)
		->SetParam("momentum", 0.9)
		->SetParam("rescale_grad", 1.0 / batch_size_)
		->SetParam("clip_gradient", 10);

	mxcpp::Xavier xavier = mxcpp::Xavier(mxcpp::Xavier::uniform, mxcpp::Xavier::avg, 1);
	for (auto& arg : exec_->arg_dict())
	{
		xavier(arg.first, &arg.second);
	}

	auto arg_names = net_.ListArguments();

	for (int e = 0; e < epoch_; ++e)
	{
		train_->BeforeFirst();
		test_->BeforeFirst();

		auto begin = std::chrono::system_clock::now();
		printf("<Epoch %d> ", e + 1);

		mxcpp::Accuracy train_acc;
		mxcpp::LogLoss train_loss;
		while (train_->Next())
		{
			train_->GetData().CopyTo(&exec_->arg_dict()["data"]);
			auto label = train_->GetLabel();
			label.CopyTo(&exec_->arg_dict()["softmax_label"]);
			mxcpp::NDArray::WaitAll();

			exec_->Forward(true);
			exec_->Backward();

			for (size_t i = 0; i < exec_->arg_arrays.size(); ++i)
				if (arg_names[i] != "data" && arg_names[i] != "softmax_label")
					opt->Update(i, exec_->arg_arrays[i], exec_->grad_arrays[i]);

			mxcpp::NDArray::WaitAll();
			train_acc.Update(label, exec_->outputs[0]);
			train_loss.Update(label, exec_->outputs[0]);
		}

		auto end = std::chrono::system_clock::now();

		std::cout << "Time Cost: " <<
			std::chrono::duration_cast<std::chrono::seconds>(end - begin).count() << " seconds" << std::endl;
		std::cout << "[Train]" << std::endl
				<< "\tAccuracy: " << train_acc.Get() << std::endl
				<< "\tLoss: " << train_loss.Get() << std::endl;

		mxcpp::Accuracy test_acc;
		mxcpp::LogLoss test_loss;
		while (test_->Next())
		{
			test_->GetData().CopyTo(&exec_->arg_dict()["data"]);
			auto label = test_->GetLabel();
			label.CopyTo(&exec_->arg_dict()["softmax_label"]);

			mxcpp::NDArray::WaitAll();
			exec_->Forward(false);
			mxcpp::NDArray::WaitAll();

			test_acc.Update(label, exec_->outputs[0]);
			test_loss.Update(label, exec_->outputs[0]);
		}

		std::cout << "[Test]" << std::endl
				<< "\tAccuracy: " << test_acc.Get() << std::endl
				<< "\tLoss: " << test_loss.Get() << std::endl;

		SaveCheckpoint(output + std::to_string(e) + ".params", net_, exec_);

		std::cout << std::endl;
	}
}

void PolicyTrainer::InitNetwork()
{
	args_["data"] = mxcpp::NDArray(mxcpp::Shape(batch_size_, process_.GetOutputDim(), 19, 19), mxcpp::Context::cpu());
	args_["softmax_label"] = mxcpp::NDArray(mxcpp::Shape(batch_size_), mxcpp::Context::cpu());
	net_.InferArgsMap(mxcpp::Context::cpu(), &args_, args_);

	exec_ = net_.SimpleBind(mxcpp::Context::cpu(), args_);
}
